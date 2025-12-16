#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

struct WatchEntry {
  std::string user;
  std::string debt_asset;
  std::string collateral_asset;
  long double usd_value = 0.0L;
  double health_factor = 1.0;
  double target_buffer = 0.05; // e.g., watch until HF <= 1 + buffer
};

class Watchlist {
public:
  // Update or insert entries from a scan; returns the entries that should be pre-staged now
  std::vector<WatchEntry> UpsertAndSelectForPrestage(const std::vector<WatchEntry>& scan,
                                                     double default_buffer);
  // Return entries that crossed into liquidatable zone (HF < 1.0)
  std::vector<WatchEntry> CollectTriggers();
  // Snapshot of all entries (for grouping/analysis)
  std::vector<WatchEntry> Snapshot() const;
private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, WatchEntry> map_; // key: user|debt|collat
};


