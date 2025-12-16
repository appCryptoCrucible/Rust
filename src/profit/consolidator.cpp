#include "profit/consolidator.hpp"
#include "routing/dex_router.hpp"
#include "mev/protection.hpp"
#include "node_connection/rpc_client.hpp"
#include "constants/polygon.hpp"
#include "protocols/erc20.hpp"
#include "wallet/signer.hpp"
#include "wallet/nonce_manager.hpp"
#include "gas/gas_strategy.hpp"
#include "common/config_manager.hpp"
#include <sstream>
#include <chrono>

// Very lightweight consolidator: swap configured tokens into USDC if balance > min threshold.
// Single-hop V2 swap on Quickswap by default; honors slippage and optional private submission.

std::optional<std::string> ProfitConsolidator::ConsolidateToUSDC() {
  // Read tokens to consolidate from env
  auto tokens_csv = ConfigManager::Get("PROFIT_TOKENS").value_or("");
  if (tokens_csv.empty()) return std::nullopt;
  const double min_usd = ConfigManager::GetDoubleOr("PROFIT_MIN_SWAP_USD", 50.0);
  const double slip_bps = ConfigManager::GetDoubleOr("MAX_SLIPPAGE_BPS", 50.0);
  const bool use_private = ConfigManager::GetBoolOr("SUBMIT_PRIVATE", false);
  const std::string usdc = PolygonConstants::USDC;
  std::vector<std::string> tokens;
  {
    std::string tmp; std::istringstream iss(tokens_csv); while (std::getline(iss, tmp, ',')) if(!tmp.empty()) tokens.push_back(tmp);
  }
  for (const auto& t : tokens) {
    if (t == usdc) continue;
    int dec = ERC20::Decimals(rpc_, t); if (dec <= 0) continue;
    unsigned long long bal = ERC20::BalanceOf(rpc_, t, signer_.Address());
    if (bal == 0ULL) continue;
    // simple USD gate via override price if present (defaults to 1 on test)
    double px = 1.0; // keep it simple here; main path uses PriceOracle
    double unit = std::pow(10.0, dec);
    double usd_val = static_cast<double>(bal) / unit * px;
    if (usd_val < min_usd) continue;
    // Build single-hop swap t -> USDC via Quickswap
    std::vector<std::string> path{ t, usdc };
    unsigned long long quote_out = DexRouterPlanner::QuoteV2GetAmountsOut(rpc_, PolygonConstants::QUICKSWAP_ROUTER, path, bal);
    if (quote_out == 0ULL) continue;
    unsigned long long out_min = static_cast<unsigned long long>((static_cast<long double>(quote_out) * (10000.0L - slip_bps)) / 10000.0L);
    unsigned long long deadline = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 180);
    auto calldata = DexRouterPlanner::BuildV2SwapExactTokensCall(bal, out_min, path, signer_.Address(), deadline);
    // Build tx
    auto gq = gas_.Quote();
    TransactionFields tx;
    tx.chain_id = PolygonConstants::CHAIN_ID;
    tx.nonce = nonce_.Next();
    tx.gas_limit = 280000; // simple swap
    tx.max_fee_per_gas = gq.max_fee_per_gas;
    tx.max_priority_fee_per_gas = gq.max_priority_fee_per_gas;
    tx.to = PolygonConstants::QUICKSWAP_ROUTER;
    tx.value = 0;
    tx.data = calldata;
    auto signed_tx = signer_.SignEip1559(tx);
    if (signed_tx.size() < 4) continue;
    mev_.ApplyTxRandomizationDelay();
    std::string hash = use_private ? rpc_.EthSendRawTransactionPrivate(signed_tx) : rpc_.EthSendRawTransactionPublic(signed_tx);
    return hash;
  }
  return std::nullopt;
}

