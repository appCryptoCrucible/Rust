#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

class RpcClient;

class PriceOracle {
public:
  // Returns USD price for token (double). Uses .env overrides: PRICE_USD_OVERRIDES=token:price,...
  // Falls back to 1.0 if unknown.
  static double GetUsdPrice(RpcClient& rpc, const std::string& token);
  static void SetOverride(const std::string& token, double price);
private:
  static std::unordered_map<std::string, double> overrides_;
  static std::mutex mutex_;
  static void LoadOverridesFromEnv();
  static bool loaded_;
};


