#pragma once
#include <string>
#include <vector>

struct SwapLeg {
  std::string router;
  std::string token_in;
  std::string token_out;
  double portion = 1.0; // fraction of total
};

struct RoutePlan {
  std::vector<SwapLeg> legs;
  double expected_price_impact_bps = 0.0;
};

class DexRouterPlanner {
public:
  // Compute best split routes across supported DEXes for multi-hop liquidation exits.
  RoutePlan PlanBest(const std::string& token_in,
                     const std::string& token_out,
                     long double amount_in,
                     double max_slippage_bps);
  // Try simple two-venue split on V2 routers (Quickswap/Sushiswap) at coarse ratios
  RoutePlan PlanBestSplitV2(class RpcClient& rpc,
                            const std::string& token_in,
                            const std::string& token_out,
                            unsigned long long amount_in_units);

  // Build Uniswap V2-like swapExactTokensForTokens calldata (0x38ed1739)
  static std::string BuildV2SwapExactTokensCall(
      unsigned long long amount_in,
      unsigned long long amount_out_min,
      const std::vector<std::string>& path,
      const std::string& to,
      unsigned long long deadline);

  // Query getAmountsOut on V2 router via eth_call; returns last amount or 0 on failure
  static unsigned long long QuoteV2GetAmountsOut(class RpcClient& rpc,
                                                 const std::string& router,
                                                 const std::vector<std::string>& path,
                                                 unsigned long long amount_in);
  // Very small cache helper for intra-block repeated quotes
  static unsigned long long QuoteV2GetAmountsOutCached(class RpcClient& rpc,
                                                       const std::string& router,
                                                       const std::vector<std::string>& path,
                                                       unsigned long long amount_in,
                                                       unsigned long long block_number);
};

