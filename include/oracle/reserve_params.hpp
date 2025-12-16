#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

struct ReserveParams {
  int liquidation_bonus_bps = 10500;  // e.g., 10500 means 5% bonus
  int close_factor_bps = 5000;    // default 50%
};

class RpcClient;

class ReserveParamsCache {
public:
  static ReserveParams Get(RpcClient& rpc, const std::string& token);
  static void SetOverride(const std::string& token, int bonus_bps, int close_factor_bps);
private:
  static std::unordered_map<std::string, ReserveParams> cache_;
  static std::mutex mutex_;
  static bool loaded_;
  static void LoadOverridesFromEnv();
};


