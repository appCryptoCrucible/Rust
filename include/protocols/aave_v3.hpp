#pragma once
#include <string>
#include <vector>

struct AavePosition {
  std::string user;
  double health_factor;
  long double debt_usd;
  std::string debt_asset;
  std::string collateral_asset;
  long double debt_amount;
  long double collateral_amount;
};

class RpcClient;
class HttpClient;

class AaveV3Scanner {
public:
  AaveV3Scanner(RpcClient& rpc, HttpClient* http, const std::string& subgraph_url)
    : rpc_(rpc), http_(http), subgraph_url_(subgraph_url) {}
  std::vector<AavePosition> ScanUnderwater(double min_usd, double max_usd);
private:
  RpcClient& rpc_;
  HttpClient* http_;
  std::string subgraph_url_;
};

