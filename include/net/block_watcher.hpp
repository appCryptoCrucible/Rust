#pragma once
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>

class RpcClient;

// Polls latest block number with exponential backoff and invokes a callback on new blocks.
class BlockWatcher {
public:
    using OnBlockFn = std::function<void(unsigned long long)>;

    BlockWatcher(RpcClient& rpc, OnBlockFn on_block)
        : rpc_(rpc), on_block_(on_block) {}

    void Start() {
        running_.store(true, std::memory_order_relaxed);
        worker_ = std::thread([this]{ this->Run(); });
    }

    void Stop() {
        running_.store(false, std::memory_order_relaxed);
        if (worker_.joinable()) worker_.join();
    }

    ~BlockWatcher() { Stop(); }

private:
    void Run();
    void RunFilterLoop();
    void RunWsLoop();

    RpcClient& rpc_;
    OnBlockFn on_block_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::string filter_id_;
};


