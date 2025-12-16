#pragma once
#include <vector>
#include <string>
#include <functional>

class RpcClient;

// Batches getUserAccountData via Multicall and returns {user, hf}
struct HFResult {
    std::string user;
    double hf;
};

class HFScanner {
public:
    HFScanner(RpcClient& rpc, const std::string& multicall, const std::string& aave_pool)
        : rpc_(rpc), multicall_(multicall), aave_pool_(aave_pool) {}

    // Returns health factors for given users. Optimized for batching.
    std::vector<HFResult> FetchHealthFactors(const std::vector<std::string>& users);

private:
    RpcClient& rpc_;
    std::string multicall_;
    std::string aave_pool_;
};


