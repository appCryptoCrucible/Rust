#pragma once
#include <cstdint>

class RpcClient;

struct GasQuote { unsigned long long max_fee_per_gas; unsigned long long max_priority_fee_per_gas; };

class GasStrategy {
public:
  explicit GasStrategy(RpcClient& rpc) : rpc_(rpc) {}
  GasQuote Quote();
private:
  RpcClient& rpc_;
};

