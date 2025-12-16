#pragma once
#include <optional>
#include <string>

class RpcClient;
class DexRouterPlanner;
class MevProtector;
class Signer;
class NonceManager;
class GasStrategy;

class ProfitConsolidator {
public:
  ProfitConsolidator(RpcClient& rpc,
                     DexRouterPlanner& router,
                     MevProtector& mev,
                     Signer& signer,
                     NonceManager& nonce,
                     GasStrategy& gas)
    : rpc_(rpc), router_(router), mev_(mev), signer_(signer), nonce_(nonce), gas_(gas) {}
  std::optional<std::string> ConsolidateToUSDC();
private:
  RpcClient& rpc_;
  DexRouterPlanner& router_;
  MevProtector& mev_;
  Signer& signer_;
  NonceManager& nonce_;
  GasStrategy& gas_;
};

