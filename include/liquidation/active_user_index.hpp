#pragma once
#include <unordered_set>
#include <shared_mutex>
#include <string>

// Lock-minimized user index of addresses that changed positions recently.
class ActiveUserIndex {
public:
    void Add(const std::string& user) {
        std::unique_lock<std::shared_mutex> lock(mu_);
        users_.insert(user);
    }
    void AddMany(const std::vector<std::string>& users) {
        std::unique_lock<std::shared_mutex> lock(mu_);
        for (const auto& u : users) users_.insert(u);
    }
    std::vector<std::string> SnapshotAndClear() {
        std::unique_lock<std::shared_mutex> lock(mu_);
        std::vector<std::string> out; out.reserve(users_.size());
        for (const auto& u : users_) out.push_back(u);
        users_.clear();
        return out;
    }
    size_t Size() const {
        std::shared_lock<std::shared_mutex> lock(mu_);
        return users_.size();
    }
private:
    mutable std::shared_mutex mu_;
    std::unordered_set<std::string> users_;
};


