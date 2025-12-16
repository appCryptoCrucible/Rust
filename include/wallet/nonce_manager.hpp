#pragma once
#include <atomic>
#include <mutex>
#include <string>

class RpcClient;

class NonceManager {
public:
  explicit NonceManager(RpcClient& rpc, const std::string& address);
  unsigned long long Next();
  void Reset(unsigned long long to);
private:
  RpcClient& rpc_;
  std::string address_;
  std::atomic<unsigned long long> current_{0};
  std::once_flag init_flag_;
  void Initialize();
};

