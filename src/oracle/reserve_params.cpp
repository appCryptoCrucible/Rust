#include "oracle/reserve_params.hpp"
#include "common/config_manager.hpp"
#include "node_connection/rpc_client.hpp"
#include <sstream>

std::unordered_map<std::string, ReserveParams> ReserveParamsCache::cache_;
std::mutex ReserveParamsCache::mutex_;
bool ReserveParamsCache::loaded_ = false;

void ReserveParamsCache::LoadOverridesFromEnv() {
  if (loaded_) return; loaded_ = true;
  if (auto v = ConfigManager::Get("RESERVE_PARAM_OVERRIDES")) {
    // token:bonus_bps:close_factor_bps,
    std::istringstream iss(*v); std::string kv;
    while (std::getline(iss, kv, ',')) {
      std::istringstream parts(kv); std::string tok, sb, sc;
      if (!std::getline(parts, tok, ':')) continue;
      if (!std::getline(parts, sb, ':')) continue;
      if (!std::getline(parts, sc, ':')) continue;
      ReserveParams rp; try { rp.liquidation_bonus_bps = std::stoi(sb); rp.close_factor_bps = std::stoi(sc); } catch (...) { continue; }
      cache_[tok] = rp;
    }
  }
}

ReserveParams ReserveParamsCache::Get(RpcClient& rpc, const std::string& token) {
  (void)rpc;
  std::lock_guard<std::mutex> lock(mutex_);
  LoadOverridesFromEnv();
  auto it = cache_.find(token);
  if (it != cache_.end()) return it->second;
  return ReserveParams{}; // default values
}

void ReserveParamsCache::SetOverride(const std::string& token, int bonus_bps, int close_factor_bps) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_[token] = ReserveParams{bonus_bps, close_factor_bps};
}


