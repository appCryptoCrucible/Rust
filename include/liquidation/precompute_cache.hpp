#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

class PrecomputeCache {
public:
  void Put(const std::string& key, const std::string& calldata_hex);
  bool Get(const std::string& key, std::string& out_calldata_hex) const;
  void Clear();
private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> map_;
};


