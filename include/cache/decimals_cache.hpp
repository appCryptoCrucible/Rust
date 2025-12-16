#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

class RpcClient;

class DecimalsCache {
public:
  static int Get(RpcClient& rpc, const std::string& token);
  static void Put(const std::string& token, int decimals);
private:
  static std::unordered_map<std::string, int> cache_;
  static std::mutex mutex_;
};


