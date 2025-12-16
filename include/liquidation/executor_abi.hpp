#pragma once
#include <string>
#include <vector>

// ABI fragment or precomputed function selectors for LiquidationExecutor.
namespace ExecutorABI {
  // keccak256("liquidateAndArb((address,address,uint256,address,(address,bytes)[],address,uint256))") first 4 bytes
  // Configurable at runtime via SetLiquidateAndArbSelector
  const std::string& GetLiquidateAndArbSelector();
  void SetLiquidateAndArbSelector(const std::string& selector0x);
  // Batch selector: keccak256("liquidateBatchAndArb((address[],address,uint256[],address,(address,bytes)[],address,uint256))")
  const std::string& GetLiquidateBatchSelector();
  void SetLiquidateBatchSelector(const std::string& selector0x);
  // Compute and initialize selectors if not set via env
  void InitializeDefaultSelectors();
  struct Swap { std::string router; std::string callDataHex; };
  struct Params {
    std::string user;
    std::string debtAsset;
    unsigned long long debtToCover;
    std::string collateralAsset;
    std::vector<Swap> swaps;
    std::string profitReceiver;
    unsigned long long minProfit;
  };

  // Build calldata (0x-prefixed hex) for liquidateAndArb(params)
  std::string BuildLiquidateAndArbCalldata(const Params& p);

  struct BatchParams {
    std::vector<std::string> users;
    std::string debtAsset;
    std::vector<unsigned long long> debtToCover;
    std::string collateralAsset;
    std::vector<Swap> swaps;
    std::string profitReceiver;
    unsigned long long minProfit;
  };
  // Build calldata for liquidateBatchAndArb(params)
  std::string BuildLiquidateBatchAndArbCalldata(const BatchParams& p);
}

