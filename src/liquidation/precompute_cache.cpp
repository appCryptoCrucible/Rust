#include "liquidation/precompute_cache.hpp"

void PrecomputeCache::Put(const std::string& key, const std::string& calldata_hex) {
  std::lock_guard<std::mutex> lock(mutex_);
  map_[key] = calldata_hex;
}

bool PrecomputeCache::Get(const std::string& key, std::string& out_calldata_hex) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = map_.find(key);
  if (it == map_.end()) return false;
  out_calldata_hex = it->second;
  return true;
}

void PrecomputeCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  map_.clear();
}


