#pragma once
#include <unordered_map>
#include <string>
#include <shared_mutex>

struct PreexecRecord {
    std::string user;
    std::string debt;
    std::string collat;
    unsigned long long debt_units = 0;
    unsigned long long collat_units = 0;
    std::string unsigned_calldata; // executor payload, unsigned
};

class PreexecCache {
public:
    void Put(const std::string& key, const PreexecRecord& r) {
        std::unique_lock<std::shared_mutex> lock(mu_);
        map_[key] = r;
    }
    bool Get(const std::string& key, PreexecRecord& out) const {
        std::shared_lock<std::shared_mutex> lock(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return false; out = it->second; return true;
    }
    void Erase(const std::string& key) {
        std::unique_lock<std::shared_mutex> lock(mu_);
        map_.erase(key);
    }
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, PreexecRecord> map_;
};


