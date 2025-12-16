#include "oracle/price_oracle.hpp"
#include "common/config_manager.hpp"
#include "node_connection/rpc_client.hpp"
#include "routing/dex_router.hpp"
#include "constants/polygon.hpp"
#include "protocols/erc20.hpp"
#include <sstream>
#include <cmath>

std::unordered_map<std::string, double> PriceOracle::overrides_;
std::mutex PriceOracle::mutex_;
bool PriceOracle::loaded_ = false;

void PriceOracle::LoadOverridesFromEnv() {
  if (loaded_) return;
  loaded_ = true;
  if (auto v = ConfigManager::Get("PRICE_USD_OVERRIDES")) {
    std::istringstream iss(*v); std::string kv;
    while (std::getline(iss, kv, ',')) {
      auto pos = kv.find(':');
      if (pos == std::string::npos) continue;
      std::string tok = kv.substr(0, pos);
      double pr = 0.0; try { pr = std::stod(kv.substr(pos+1)); } catch (...) { continue; }
      overrides_[tok] = pr;
    }
  }
}

double PriceOracle::GetUsdPrice(RpcClient& rpc, const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  LoadOverridesFromEnv();
  auto it = overrides_.find(token);
  if (it != overrides_.end()) return it->second;

  // Live DEX-based pricing: quote token -> USDC using V2 routers; fallback via WMATIC
  try {
    // Normalize token_in for WMATIC/native
    std::string token_in = token;
    if (token == "MATIC") token_in = PolygonConstants::WMATIC; // treat native as WMATIC
    // If already USDC, price is 1
    if (token_in == PolygonConstants::USDC) return 1.0;
    // Determine token decimals (default to 18 on failure)
    int dec = 18;
    try { dec = ERC20::Decimals(rpc, token_in); if (dec <= 0) dec = 18; } catch (...) { dec = 18; }
    unsigned long long one_unit = static_cast<unsigned long long>(std::pow(10.0L, dec));
    // Direct path token -> USDC
    unsigned long long out_direct = DexRouterPlanner::QuoteV2GetAmountsOut(rpc, PolygonConstants::QUICKSWAP_ROUTER, { token_in, PolygonConstants::USDC }, one_unit);
    if (out_direct == 0ULL) out_direct = DexRouterPlanner::QuoteV2GetAmountsOut(rpc, PolygonConstants::SUSHISWAP_ROUTER, { token_in, PolygonConstants::USDC }, one_unit);
    if (out_direct) {
      return static_cast<double>(out_direct) / 1e6; // USDC has 6 decimals
    }
    // Fallback via WMATIC: token -> WMATIC -> USDC
    unsigned long long to_wmatic = DexRouterPlanner::QuoteV2GetAmountsOut(rpc, PolygonConstants::QUICKSWAP_ROUTER, { token_in, PolygonConstants::WMATIC }, one_unit);
    if (to_wmatic == 0ULL) to_wmatic = DexRouterPlanner::QuoteV2GetAmountsOut(rpc, PolygonConstants::SUSHISWAP_ROUTER, { token_in, PolygonConstants::WMATIC }, one_unit);
    if (to_wmatic) {
      unsigned long long to_usdc = DexRouterPlanner::QuoteV2GetAmountsOut(rpc, PolygonConstants::QUICKSWAP_ROUTER, { PolygonConstants::WMATIC, PolygonConstants::USDC }, to_wmatic);
      if (to_usdc == 0ULL) to_usdc = DexRouterPlanner::QuoteV2GetAmountsOut(rpc, PolygonConstants::SUSHISWAP_ROUTER, { PolygonConstants::WMATIC, PolygonConstants::USDC }, to_wmatic);
      if (to_usdc) return static_cast<double>(to_usdc) / 1e6;
    }
  } catch (...) {
    // ignore, fallback to default below
  }

  // Final fallback
  return 1.0;
}

void PriceOracle::SetOverride(const std::string& token, double price) {
  std::lock_guard<std::mutex> lock(mutex_);
  overrides_[token] = price;
}


