#include "cache/decimals_cache.hpp"
#include "protocols/erc20.hpp"
#include "node_connection/rpc_client.hpp"

std::unordered_map<std::string, int> DecimalsCache::cache_;
std::mutex DecimalsCache::mutex_;

int DecimalsCache::Get(RpcClient& rpc, const std::string& token) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cache_.find(token);
  if (it != cache_.end()) return it->second;
  int d = ERC20::Decimals(rpc, token);
  if (d <= 0) d = 18; // default safe fallback
  cache_[token] = d;
  return d;
}

void DecimalsCache::Put(const std::string& token, int decimals) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_[token] = decimals;
}


