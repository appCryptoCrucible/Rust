#include "liquidation/watchlist.hpp"

static std::string KeyOf(const WatchEntry& e) {
  return e.user + "|" + e.debt_asset + "|" + e.collateral_asset;
}

std::vector<WatchEntry> Watchlist::UpsertAndSelectForPrestage(const std::vector<WatchEntry>& scan,
                                                              double default_buffer) {
  std::vector<WatchEntry> prestage;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& e : scan) {
    auto key = KeyOf(e);
    auto it = map_.find(key);
    WatchEntry copy = e;
    if (copy.target_buffer <= 0.0) copy.target_buffer = default_buffer;
    map_[key] = copy;
    if (copy.health_factor <= 1.0 + copy.target_buffer) {
      prestage.push_back(copy);
    }
  }
  return prestage;
}

std::vector<WatchEntry> Watchlist::CollectTriggers() {
  std::vector<WatchEntry> triggers;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& kv : map_) {
    if (kv.second.health_factor < 1.0) triggers.push_back(kv.second);
  }
  return triggers;
}

std::vector<WatchEntry> Watchlist::Snapshot() const {
  std::vector<WatchEntry> v;
  std::lock_guard<std::mutex> lock(mutex_);
  v.reserve(map_.size());
  for (const auto& kv : map_) v.push_back(kv.second);
  return v;
}


