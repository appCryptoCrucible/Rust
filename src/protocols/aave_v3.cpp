#include "protocols/aave_v3.hpp"
#include "node_connection/rpc_client.hpp"
#include "common/config_manager.hpp"
#include "crypto/keccak.hpp"
#include "oracle/price_oracle.hpp"
#include "net/http_client.hpp"
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

static std::string Pad32(const std::string& no0x) {
  if (no0x.size() >= 64) return no0x.substr(no0x.size() - 64);
  return std::string(64 - no0x.size(), '0') + no0x;
}

static std::string EncodeGetUserAccountData(const std::string& user) {
  // selector keccak("getUserAccountData(address)")
  auto sel = Crypto::Keccak256Raw("getUserAccountData(address)");
  std::string s = (sel.rfind("0x",0)==0?sel.substr(2):sel).substr(0,8);
  std::string u = user.rfind("0x",0)==0?user.substr(2):user;
  std::ostringstream oss;
  oss << "0x" << s << Pad32(u);
  return oss.str();
}

static unsigned long long HexToULL(const std::string& h) {
  std::string s = (h.rfind("0x",0)==0?h.substr(2):h);
  if (s.empty()) return 0ULL;
  return std::stoull(s, nullptr, 16);
}

std::vector<AavePosition> AaveV3Scanner::ScanUnderwater(double min_usd, double max_usd) {
  std::vector<AavePosition> out;

  // Prefer subgraph on mainnet when configured; fallback to minimal on-chain scanner
  if (http_ && !subgraph_url_.empty()) {
    try {
      // GraphQL: fetch users with HF < 1 and their reserves
      // Note: Aave v3 subgraph schemas vary slightly; we defensively access fields.
      const std::string gql = R"({
        "query":"query { users(where:{ healthFactor_lt: \"1\" }, first: 500) { id healthFactor userReserves { currentATokenBalance scaledVariableDebt reserve { underlyingAsset symbol decimals usageAsCollateralEnabled } } } }"
      })";
      std::unordered_map<std::string, std::string> headers{{"Content-Type","application/json"}};
      auto resp = http_->Post(subgraph_url_, gql, headers, 5000);
      if (resp.status == 200 && !resp.body.empty()) {
        auto j = nlohmann::json::parse(resp.body, nullptr, false);
        if (j.is_object() && j.contains("data") && j["data"].contains("users")) {
          for (const auto& u : j["data"]["users"]) {
            // Parse HF (may be string or number); treat <1 as underwater
            double hf = 0.0;
            try {
              if (u.contains("healthFactor")) {
                if (u["healthFactor"].is_string()) {
                  hf = std::stod(static_cast<std::string>(u["healthFactor"]));
                } else if (u["healthFactor"].is_number()) {
                  hf = u["healthFactor"].get<double>();
                }
              }
            } catch (...) { hf = 0.0; }
            if (!(hf < 1.0)) continue;

            std::string user = u.contains("id") && u["id"].is_string() ? static_cast<std::string>(u["id"]) : std::string();
            if (user.empty()) continue;

            std::vector<nlohmann::json> debt_reserves;
            std::vector<nlohmann::json> collat_reserves;
            if (u.contains("userReserves") && u["userReserves"].is_array()) {
              for (const auto& ur : u["userReserves"]) {
                // Expect BigInt strings; consider >0 as present
                auto gt_zero = [](const nlohmann::json& v){ try { if (v.is_string()) return std::stold(v.get<std::string>()) > 0.0L; if (v.is_number()) return v.get<long double>() > 0.0L; } catch (...) {} return false; };
                bool hasDebt = ur.contains("scaledVariableDebt") && gt_zero(ur["scaledVariableDebt"]);
                bool hasCollat = ur.contains("currentATokenBalance") && gt_zero(ur["currentATokenBalance"]);
                if (!ur.contains("reserve")) continue;
                const auto& res = ur["reserve"];
                bool collatEnabled = res.contains("usageAsCollateralEnabled") ? res["usageAsCollateralEnabled"].get<bool>() : true;
                if (hasDebt) debt_reserves.push_back(ur);
                if (hasCollat && collatEnabled) collat_reserves.push_back(ur);
              }
            }

            // Cross debt and collateral reserves
            for (const auto& d : debt_reserves) {
              const auto& dres = d["reserve"];
              std::string d_addr = dres.contains("underlyingAsset") ? dres["underlyingAsset"].get<std::string>() : std::string();
              int d_dec = dres.contains("decimals") ? std::stoi(dres["decimals"].get<std::string>()) : 18;
              long double d_amt_units = 0.0L;
              try { d_amt_units = d.contains("scaledVariableDebt") ? std::stold(d["scaledVariableDebt"].get<std::string>()) : 0.0L; } catch (...) { d_amt_units = 0.0L; }
              double d_px = PriceOracle::GetUsdPrice(rpc_, d_addr);
              if (d_px <= 0.0) d_px = ConfigManager::GetDoubleOr("DEBT_USD_PRICE", 1.0);
              long double d_amt = d_amt_units / std::pow(10.0L, d_dec);
              long double d_usd = d_amt * static_cast<long double>(d_px);
              if (d_usd < static_cast<long double>(min_usd) || d_usd > static_cast<long double>(max_usd)) continue;

              for (const auto& c : collat_reserves) {
                const auto& cres = c["reserve"];
                std::string c_addr = cres.contains("underlyingAsset") ? cres["underlyingAsset"].get<std::string>() : std::string();
                if (c_addr == d_addr) continue;
                int c_dec = cres.contains("decimals") ? std::stoi(cres["decimals"].get<std::string>()) : 18;
                long double c_amt_units = 0.0L;
                try { c_amt_units = c.contains("currentATokenBalance") ? std::stold(c["currentATokenBalance"].get<std::string>()) : 0.0L; } catch (...) { c_amt_units = 0.0L; }
                // Build approximate position
                AavePosition p;
                p.user = user;
                p.health_factor = hf;
                p.debt_asset = d_addr;
                p.collateral_asset = c_addr;
                p.debt_amount = d_amt_units; // raw units, manager will convert with decimals later
                p.collateral_amount = c_amt_units; // raw units
                p.debt_usd = d_usd;
                out.push_back(std::move(p));
              }
            }
          }
        }
      }
    } catch (...) {
      // swallow and fallback
    }
  }

  if (!out.empty()) return out;

  // Fallback: Minimal on-chain scan based on MONITOR_USERS list and configured pool address
  std::string pool = ConfigManager::Get("TESTNET_AAVE_POOL").value_or(ConfigManager::Get("AAVE_POOL").value_or(""));
  if (pool.empty()) return out;
  auto users_csv = ConfigManager::Get("MONITOR_USERS").value_or("");
  if (users_csv.empty()) return out;
  auto debts_csv = ConfigManager::Get("DEBT_ASSETS").value_or(ConfigManager::Get("DEFAULT_DEBT_ASSET").value_or(""));
  auto collats_csv = ConfigManager::Get("COLLATERAL_ASSETS").value_or(ConfigManager::Get("DEFAULT_COLLATERAL_ASSET").value_or(""));
  std::vector<std::string> debt_assets; { std::string tmp; std::istringstream iss(debts_csv); while (std::getline(iss, tmp, ',')) if(!tmp.empty()) debt_assets.push_back(tmp); }
  std::vector<std::string> collat_assets; { std::string tmp; std::istringstream iss(collats_csv); while (std::getline(iss, tmp, ',')) if(!tmp.empty()) collat_assets.push_back(tmp); }
  if (debt_assets.empty() || collat_assets.empty()) return out;
  std::vector<std::string> users; { std::string tmp; std::istringstream iss(users_csv); while (std::getline(iss, tmp, ',')) { if (!tmp.empty()) users.push_back(tmp); } }
  for (const auto& u : users) {
    auto data = EncodeGetUserAccountData(u);
    try {
      auto res = rpc_.EthCall(pool, data, std::nullopt, 1000);
      std::string r = (res.rfind("0x",0)==0?res.substr(2):res);
      if (r.size() < 64*6) continue;
      auto field = [&](int idx){ return std::string(r.substr(idx*64,64)); };
      unsigned long long totalDebtBase = HexToULL(field(1)); // 1: totalDebtBase
      unsigned long long hf_raw = HexToULL(field(5)); // 5: healthFactor
      double hf = static_cast<double>(hf_raw) / 1e18;
      long double debt_usd = static_cast<long double>(totalDebtBase) / 1e8L;
      if (debt_usd < static_cast<long double>(min_usd) || debt_usd > static_cast<long double>(max_usd)) continue;
      for (const auto& d : debt_assets) {
        for (const auto& c : collat_assets) {
          if (d == c) continue;
          AavePosition p; p.user = u; p.health_factor = hf; p.debt_usd = debt_usd; p.debt_asset = d; p.collateral_asset = c; p.debt_amount = static_cast<long double>(totalDebtBase) / 1e2L; p.collateral_amount = p.debt_amount; out.push_back(std::move(p));
        }
      }
    } catch (...) { continue; }
  }
  return out;
}


