#pragma once
#include <string>

class RpcClient;

namespace ERC20 {
  // Returns decimals() via eth_call; 0 on failure
  int Decimals(RpcClient& rpc, const std::string& token);
  // Returns balanceOf(owner) via eth_call; 0 on failure
  unsigned long long BalanceOf(RpcClient& rpc, const std::string& token, const std::string& owner);
  // Returns allowance(owner, spender); 0 on failure
  unsigned long long Allowance(RpcClient& rpc, const std::string& token, const std::string& owner, const std::string& spender);
}


