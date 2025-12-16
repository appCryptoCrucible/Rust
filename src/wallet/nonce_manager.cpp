#include "wallet/nonce_manager.hpp"
#include "node_connection/rpc_client.hpp"
#include <string>

static unsigned long long ParseHexResultToULL(const std::string& json) {
  // naive parse: find "result":"0x..."
  auto pos = json.find("\"result\"");
  if (pos == std::string::npos) return 0ULL;
  pos = json.find('"', pos + 8);
  if (pos == std::string::npos) return 0ULL;
  auto pos2 = json.find('"', pos + 1);
  if (pos2 == std::string::npos) return 0ULL;
  std::string val = json.substr(pos + 1, pos2 - pos - 1);
  if (val.rfind("0x", 0) == 0) val = val.substr(2);
  if (val.empty()) return 0ULL;
  return std::stoull(val, nullptr, 16);
}

NonceManager::NonceManager(RpcClient& rpc, const std::string& address) : rpc_(rpc), address_(address) {}

void NonceManager::Initialize() {
  auto json = rpc_.EthGetTransactionCount(address_, "pending");
  auto n = ParseHexResultToULL(json);
  current_.store(n);
}

unsigned long long NonceManager::Next() {
  std::call_once(init_flag_, [this]{ Initialize(); });
  return current_.fetch_add(1);
}

void NonceManager::Reset(unsigned long long to) { current_.store(to); }

