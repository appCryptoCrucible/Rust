#include "mev/protection.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

std::string MevProtector::WrapRawTxForPrivateRelay(const std::string& signed_tx_rlp_hex) const {
  // Private relays that accept raw transactions over JSON-RPC typically expect the normal raw RLP hex.
  // If a specific relay requires an envelope, integrate it here. Default: pass-through.
  (void)cfg_;
  return signed_tx_rlp_hex;
}

bool MevProtector::ShouldAbortDueToSandwichRisk(double observed_price_impact_bps) const {
  if (!cfg_.enable_sandwich_guard) return false;
  // Simple heuristic threshold; should be augmented with mempool simulation.
  return observed_price_impact_bps > cfg_.max_slippage_bps * 1.5;
}

double MevProtector::ClampSlippageBps(double requested_bps) const {
  return std::min(requested_bps, cfg_.max_slippage_bps);
}

static void BusyWaitNs(uint32_t ns) {
  if (ns == 0) return;
  auto start = std::chrono::high_resolution_clock::now();
  auto target = start + std::chrono::nanoseconds(ns);
  while (std::chrono::high_resolution_clock::now() < target) {
    // busy wait
  }
}

void MevProtector::ApplyTxRandomizationDelay() const {
  if (cfg_.backrun_delay_ns > 0) BusyWaitNs(cfg_.backrun_delay_ns);
}

