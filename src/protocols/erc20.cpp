#include "protocols/erc20.hpp"
#include "node_connection/rpc_client.hpp"
#include <string>

static std::string Pad32(const std::string& no0x) {
  if (no0x.size() >= 64) return no0x.substr(no0x.size() - 64);
  return std::string(64 - no0x.size(), '0') + no0x;
}

namespace ERC20 {
  int Decimals(RpcClient& rpc, const std::string& token) {
    try {
      // decimals() -> 0x313ce567
      auto res = rpc.EthCall(token, "0x313ce567", std::nullopt, 500);
      if (res.size() < 66) return 0;
      std::string s = (res.rfind("0x",0)==0?res.substr(2):res);
      return std::stoi(s, nullptr, 16);
    } catch (...) { return 0; }
  }
  unsigned long long BalanceOf(RpcClient& rpc, const std::string& token, const std::string& owner) {
    try {
      // balanceOf(address) -> 0x70a08231
      std::string o = owner.rfind("0x",0)==0?owner.substr(2):owner;
      auto data = std::string("0x70a08231") + Pad32(o);
      auto res = rpc.EthCall(token, data, std::nullopt, 800);
      std::string s = (res.rfind("0x",0)==0?res.substr(2):res);
      if (s.empty()) return 0ULL;
      return std::stoull(s, nullptr, 16);
    } catch (...) { return 0ULL; }
  }
  unsigned long long Allowance(RpcClient& rpc, const std::string& token, const std::string& owner, const std::string& spender) {
    try {
      // allowance(address,address) -> 0xdd62ed3e
      std::string o = owner.rfind("0x",0)==0?owner.substr(2):owner;
      std::string s = spender.rfind("0x",0)==0?spender.substr(2):spender;
      auto data = std::string("0xdd62ed3e") + Pad32(o) + Pad32(s);
      auto res = rpc.EthCall(token, data, std::nullopt, 800);
      std::string r = (res.rfind("0x",0)==0?res.substr(2):res);
      if (r.empty()) return 0ULL;
      return std::stoull(r, nullptr, 16);
    } catch (...) { return 0ULL; }
  }
}


