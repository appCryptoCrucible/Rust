#pragma once
#include <cstdint>
#include <string>

struct MevProtectionConfig {
  bool enable_tx_randomization = true;
  bool use_private_tx = true; // send to Nodies private tx endpoint if available
  uint32_t backrun_delay_ns = 0; // optional spin wait to randomize timing
  double max_slippage_bps = 50.0; // 0.5%
  bool enable_sandwich_guard = true;
};

class MevProtector {
public:
  explicit MevProtector(MevProtectionConfig cfg) : cfg_(cfg) {}
  std::string WrapRawTxForPrivateRelay(const std::string& signed_tx_rlp_hex) const;
  bool ShouldAbortDueToSandwichRisk(double observed_price_impact_bps) const;
  double ClampSlippageBps(double requested_bps) const;
  void ApplyTxRandomizationDelay() const;
private:
  MevProtectionConfig cfg_;
};

