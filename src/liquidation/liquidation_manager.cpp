#include "liquidation/liquidation_manager.hpp"
#include "node_connection/rpc_client.hpp"
#include "mev/protection.hpp"
#include "telemetry/csv_logger.hpp"
#include "routing/dex_router.hpp"
#include "routing/reserves_cache.hpp"
#include "wallet/signer.hpp"
#include "wallet/nonce_manager.hpp"
#include "gas/gas_strategy.hpp"
#include "constants/polygon.hpp"
#include "liquidation/executor_abi.hpp"
#include "protocols/aave_v3.hpp"
#include "profit/consolidator.hpp"
#include "liquidation/watchlist.hpp"
#include "common/logger.hpp"
#include "common/config_manager.hpp"
#include "cache/decimals_cache.hpp"
#include "telemetry/structured_logger.hpp"
#include "oracle/price_oracle.hpp"
#include "oracle/reserve_params.hpp"
#include "liquidation/hf_scanner.hpp"
#include "liquidation/precompute_cache.hpp"
#include <cmath>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

LiquidationManager::LiquidationManager(RpcClient& rpc,
                                       MevProtector& mev,
                                       CsvLogger& logger,
                                       DexRouterPlanner& router,
                                       Signer& signer,
                                       NonceManager& nonce_manager,
                                       GasStrategy& gas_strategy,
                                       const std::string& executor_address,
                                       ProfitConsolidator& consolidator,
                                       HttpClient* http_client,
                                       bool dry_run,
                                       PrecomputeCache& cache,
                                       GasEscalator& escalator,
                                       MultiRelaySender* multi_relay)
  : rpc_(rpc), mev_(mev), logger_(logger), router_(router), signer_(signer),
    executor_address_(executor_address), nonce_manager_(nonce_manager), gas_strategy_(gas_strategy),
    consolidator_(consolidator), http_(http_client), dry_run_(dry_run),
    cache_(cache), escalator_(escalator), multi_relay_(multi_relay) {}

std::vector<LiquidationTarget> LiquidationManager::ScanEligible(double min_usd, double max_usd) {
  // This is now deprecated and will be removed. The main loop drives scanning.
  return {};
}

void LiquidationManager::ConfigureLimits(int max_targets_per_tick,
                       double filter_min_usd_sim,
                       double filter_preferred_max_usd) {
  max_targets_per_tick_ = max_targets_per_tick;
  filter_min_usd_sim_ = filter_min_usd_sim;
  filter_preferred_max_usd_ = filter_preferred_max_usd;
}

void LiquidationManager::PrecomputeCalldataFor(const std::string& user,
                                               const std::string& debt_asset,
                                               const std::string& collateral_asset) {
  // This logic remains valid.
  ExecutorABI::Params p;
  p.user = user;
  p.debtAsset = debt_asset;
  p.collateralAsset = collateral_asset;
  p.debtToCover = 0; // filled at execution time
  p.profitReceiver = signer_.Address();
  p.minProfit = 1;
  std::string key = user + ":" + debt_asset + ":" + collateral_asset;
  std::string unused;
  if (!cache_.Get(key, unused)) {
    auto calldata = ExecutorABI::BuildLiquidateAndArbCalldata(p);
    cache_.Put(key, calldata);
  }
}

bool LiquidationManager::BuildAtomicTxFields(const LiquidationTarget& t, double max_slippage_bps, TransactionFields& tx) {
  // Enforce per-user minimum liquidation amount in USD
  const double min_liq_usd = ConfigManager::GetDoubleOr("MIN_LIQ_USD", 100.0);
  const double max_liq_usd = ConfigManager::GetDoubleOr("MAX_LIQ_USD", 51000.0);
  const int debt_decimals = DecimalsCache::Get(rpc_, t.debt_asset);
  const int collat_decimals = DecimalsCache::Get(rpc_, t.collateral_asset);
  // Respect reserve close factor; clamp to configured USD window
  auto reserve_params = ReserveParamsCache::Get(rpc_, t.debt_asset);
  long double capped_repay_usd = (static_cast<long double>(reserve_params.close_factor_bps) * t.usd_value) / 10000.0L;
  long double repay_usd = std::min(static_cast<long double>(max_liq_usd), std::max(static_cast<long double>(min_liq_usd), capped_repay_usd));
  if (repay_usd < static_cast<long double>(min_liq_usd)) return false;

  // Convert repay USD into token units using oracle prices (defaults to 1.0 on testnet)
  double debt_price = PriceOracle::GetUsdPrice(rpc_, t.debt_asset);
  double collat_price = PriceOracle::GetUsdPrice(rpc_, t.collateral_asset);
  if (debt_price <= 0.0) debt_price = 1.0;
  if (collat_price <= 0.0) collat_price = 1.0;
  unsigned long long debt_units = static_cast<unsigned long long>((repay_usd / debt_price) * std::pow(10.0L, debt_decimals));
  unsigned long long collat_units = static_cast<unsigned long long>((repay_usd / collat_price) * std::pow(10.0L, collat_decimals));

  // Quote slippage via V2 routers
  std::vector<std::string> path{ t.collateral_asset, t.debt_asset };
  unsigned long long current_block = 0ULL; try { current_block = std::stoull(rpc_.EthBlockNumber(400), nullptr, 16); } catch (...) { current_block = 0ULL; }
  static thread_local V2ReservesCache v2res;
  unsigned long long q_quick = v2res.QuoteV2Local(rpc_, PolygonConstants::QUICKSWAP_FACTORY, t.collateral_asset, t.debt_asset, collat_units, current_block);
  if (q_quick == 0ULL) q_quick = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::QUICKSWAP_ROUTER, path, collat_units, current_block);
  unsigned long long q_sushi = v2res.QuoteV2Local(rpc_, PolygonConstants::SUSHISWAP_FACTORY, t.collateral_asset, t.debt_asset, collat_units, current_block);
  if (q_sushi == 0ULL) q_sushi = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::SUSHISWAP_ROUTER, path, collat_units, current_block);
  unsigned long long quoted_out = q_quick ? q_quick : q_sushi;
  if (quoted_out == 0ULL) {
    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    std::string j = std::string("{") +
      "\"event\":\"skip_reason\"," +
      "\"ts_ms\":" + std::to_string(now) + "," +
      "\"pair\":\"" + t.collateral_asset + "/" + t.debt_asset + "\"," +
      "\"user\":\"" + t.user + "\"," +
      "\"usd_value\":" + std::to_string(static_cast<double>(t.usd_value)) + "," +
      "\"reason\":\"insufficient_liquidity\"" +
      "}";
    StructuredLogger::Instance().LogJsonLine(j);
    return false;
  }

  double slip = mev_.ClampSlippageBps(max_slippage_bps);
  unsigned long long deadline = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 180);
  const double split_trigger_usd = ConfigManager::GetDoubleOr("SPLIT_TRIGGER_USD", 15000.0);
  std::vector<ExecutorABI::Swap> swaps;
  unsigned long long amount_out_min_total = 0ULL;
  if (repay_usd >= static_cast<long double>(split_trigger_usd)) {
    auto plan = router_.PlanBestSplitV2(rpc_, t.collateral_asset, t.debt_asset, collat_units);
    for (const auto& leg : plan.legs) {
      unsigned long long in_leg = static_cast<unsigned long long>(static_cast<long double>(collat_units) * leg.portion);
      if (in_leg == 0ULL) continue;
      std::vector<std::string> p{ t.collateral_asset, t.debt_asset };
      unsigned long long q_leg = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, leg.router, p, in_leg, current_block);
      unsigned long long out_min_leg = static_cast<unsigned long long>((static_cast<long double>(q_leg) * (10000.0L - slip)) / 10000.0L);
      amount_out_min_total += out_min_leg;
      auto calldata = DexRouterPlanner::BuildV2SwapExactTokensCall(in_leg, out_min_leg, p, executor_address_, deadline);
      swaps.push_back({ leg.router, calldata });
    }
  }
  if (swaps.empty()) {
    unsigned long long amount_out_min = static_cast<unsigned long long>((static_cast<long double>(quoted_out) * (10000.0L - slip)) / 10000.0L);
    std::string router = q_quick ? PolygonConstants::QUICKSWAP_ROUTER : PolygonConstants::SUSHISWAP_ROUTER;
    auto swap_calldata = DexRouterPlanner::BuildV2SwapExactTokensCall(collat_units, amount_out_min, path, executor_address_, deadline);
    swaps.push_back({ router, swap_calldata });
    amount_out_min_total = amount_out_min;
  }
  // Telemetry: route_quote
  {
    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    std::string j = std::string("{") +
      "\"event\":\"route_quote\"," +
      "\"ts_ms\":" + std::to_string(now) + "," +
      "\"pair\":\"" + t.collateral_asset + "/" + t.debt_asset + "\"," +
      "\"amount_in_units\":" + std::to_string(collat_units) + "," +
      "\"quotes\":[{" +
        "\"dex\":\"Quickswap\",\"out_units\":" + std::to_string(q_quick) + "},{" +
        "\"dex\":\"Sushiswap\",\"out_units\":" + std::to_string(q_sushi) + "}]," +
      "\"selected_dex\":\"" + (q_quick?"Quickswap":"Sushiswap") + "\"" +
      "}";
    StructuredLogger::Instance().LogJsonLine(j);
  }

  // Profitability guard: ensure proceeds cover debt + premium + gas
  // Premium 0.09% of debt
  unsigned long long premium_units = static_cast<unsigned long long>((static_cast<long double>(debt_units) * 9.0L) / 10000.0L);
  auto gq = gas_strategy_.Quote();
  unsigned long long gas_limit_est = 1'900'000ULL;
  // Convert gas (WMATIC) directly to debt units via live DEX quotes
  unsigned long long gas_fee_wei = gq.max_fee_per_gas;
  long double matic_units = (static_cast<long double>(gas_limit_est) * static_cast<long double>(gas_fee_wei)) / 1e18L;
  unsigned long long matic_units_wei = static_cast<unsigned long long>(matic_units * std::pow(10.0L, 18));
  unsigned long long out_direct_q = v2res.QuoteV2Local(rpc_, PolygonConstants::QUICKSWAP_FACTORY, PolygonConstants::WMATIC, t.debt_asset, matic_units_wei, current_block);
  if (out_direct_q == 0ULL) out_direct_q = v2res.QuoteV2Local(rpc_, PolygonConstants::SUSHISWAP_FACTORY, PolygonConstants::WMATIC, t.debt_asset, matic_units_wei, current_block);
  if (out_direct_q == 0ULL) out_direct_q = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::QUICKSWAP_ROUTER, { PolygonConstants::WMATIC, t.debt_asset }, matic_units_wei, current_block);
  if (out_direct_q == 0ULL) out_direct_q = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::SUSHISWAP_ROUTER, { PolygonConstants::WMATIC, t.debt_asset }, matic_units_wei, current_block);
  unsigned long long out_via_usdc = 0ULL;
  if (out_direct_q == 0ULL) {
    unsigned long long to_usdc = v2res.QuoteV2Local(rpc_, PolygonConstants::QUICKSWAP_FACTORY, PolygonConstants::WMATIC, PolygonConstants::USDC, matic_units_wei, current_block);
    if (to_usdc == 0ULL) to_usdc = v2res.QuoteV2Local(rpc_, PolygonConstants::SUSHISWAP_FACTORY, PolygonConstants::WMATIC, PolygonConstants::USDC, matic_units_wei, current_block);
    if (to_usdc == 0ULL) to_usdc = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::QUICKSWAP_ROUTER, { PolygonConstants::WMATIC, PolygonConstants::USDC }, matic_units_wei, current_block);
    if (to_usdc == 0ULL) to_usdc = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::SUSHISWAP_ROUTER, { PolygonConstants::WMATIC, PolygonConstants::USDC }, matic_units_wei, current_block);
    if (to_usdc) {
      out_via_usdc = v2res.QuoteV2Local(rpc_, PolygonConstants::QUICKSWAP_FACTORY, PolygonConstants::USDC, t.debt_asset, to_usdc, current_block);
      if (out_via_usdc == 0ULL) out_via_usdc = v2res.QuoteV2Local(rpc_, PolygonConstants::SUSHISWAP_FACTORY, PolygonConstants::USDC, t.debt_asset, to_usdc, current_block);
      if (out_via_usdc == 0ULL) out_via_usdc = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::QUICKSWAP_ROUTER, { PolygonConstants::USDC, t.debt_asset }, to_usdc, current_block);
      if (out_via_usdc == 0ULL) out_via_usdc = DexRouterPlanner::QuoteV2GetAmountsOutCached(rpc_, PolygonConstants::SUSHISWAP_ROUTER, { PolygonConstants::USDC, t.debt_asset }, to_usdc, current_block);
    }
  }
  long double gas_cost_in_debt_units = static_cast<long double>(out_direct_q ? out_direct_q : out_via_usdc);
  unsigned long long required_units = debt_units + premium_units + static_cast<unsigned long long>(gas_cost_in_debt_units);
  if (amount_out_min_total < required_units) {
    auto now2 = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    std::string j2 = std::string("{") +
      "\"event\":\"skip_reason\"," +
      "\"ts_ms\":" + std::to_string(now2) + "," +
      "\"pair\":\"" + t.collateral_asset + "/" + t.debt_asset + "\"," +
      "\"user\":\"" + t.user + "\"," +
      "\"usd_value\":" + std::to_string(static_cast<double>(t.usd_value)) + "," +
      "\"reason\":\"profit_guard\"" +
      "}";
    StructuredLogger::Instance().LogJsonLine(j2);
    return false;
  }

  ExecutorABI::Params p;
  p.user = t.user;
  p.debtAsset = t.debt_asset;
  p.debtToCover = debt_units;
  p.collateralAsset = t.collateral_asset;
  p.swaps = swaps;
  p.profitReceiver = signer_.Address();
  p.minProfit = 1;

  auto calldata = ExecutorABI::BuildLiquidateAndArbCalldata(p);
  // Telemetry: tx_built
  {
    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    std::string j = std::string("{") +
      "\"event\":\"tx_built\"," +
      "\"ts_ms\":" + std::to_string(now) + "," +
      "\"tx_kind\":\"single\"," +
      "\"pair\":\"" + t.collateral_asset + "/" + t.debt_asset + "\"," +
      "\"users_count\":1," +
      "\"debt_units_total\":" + std::to_string(debt_units) + "," +
      "\"amount_out_min_units\":" + std::to_string(amount_out_min_total) +
      "}";
    StructuredLogger::Instance().LogJsonLine(j);
  }
  // Fill tx fields (sign later)
  tx.chain_id = PolygonConstants::CHAIN_ID;
  tx.nonce = nonce_manager_.Next();
  tx.gas_limit = 1'900'000;
  tx.max_fee_per_gas = gq.max_fee_per_gas;
  tx.max_priority_fee_per_gas = gq.max_priority_fee_per_gas;
  tx.to = executor_address_;
  tx.value = 0;
  tx.data = calldata;
  return true;
}

ExecutionResult LiquidationManager::ExecuteAtomic(const LiquidationTarget& t, double max_slippage_bps) {
  ExecutionResult res;
  try {
    TransactionFields tx;
    if (!BuildAtomicTxFields(t, max_slippage_bps, tx)) return res;
    if (!dry_run_) {
      std::string tx_hash;
      if (!SubmitWithRbf(tx, tx_hash)) return res;
      res.submitted = true; res.tx_hash = tx_hash;
      // Telemetry: tx_submitted
      auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
      std::string j = std::string("{") +
        "\"event\":\"tx_submitted\"," +
        "\"ts_ms\":" + std::to_string(now) + "," +
        "\"tx_hash\":\"" + tx_hash + "\"," +
        "\"nonce\":" + std::to_string(tx.nonce) + "," +
        "\"submit_kind\":\"public\"," +
        "\"rbf_index\":0," +
        "\"max_fee_per_gas\":" + std::to_string(tx.max_fee_per_gas) + "," +
        "\"max_priority_fee\":" + std::to_string(tx.max_priority_fee_per_gas) +
        "}";
      StructuredLogger::Instance().LogJsonLine(j);
    }
    // TODO: wait for receipt or subscribe via ws; compute realized profit
    res.success = true;
  } catch (const std::exception& ex) {
    Logger::Error(std::string("ExecuteAtomic failed: ") + ex.what());
  }
  return res;
}

std::optional<std::string> LiquidationManager::ConsolidateProfitsToUSDC() {
  return consolidator_.ConsolidateToUSDC();
}

bool LiquidationManager::WaitForReceipt(const std::string& tx_hash, int timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < timeout_ms) {
    try {
      auto r = rpc_.EthGetTransactionReceipt(tx_hash, 800);
      if (!r.empty() && r != "null") {
        // Telemetry: tx_receipt (minimal)
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
        std::string j = std::string("{") +
          "\"event\":\"tx_receipt\"," +
          "\"ts_ms\":" + std::to_string(now) + "," +
          "\"tx_hash\":\"" + tx_hash + "\"" +
          "}";
        StructuredLogger::Instance().LogJsonLine(j);
        return true;
      }
    } catch (...) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return false;
}

bool LiquidationManager::SubmitWithRbf(TransactionFields base_tx, std::string& out_tx_hash) {
  double bump = ConfigManager::GetDoubleOr("RBF_BUMP_FACTOR", 1.2);
  int interval = ConfigManager::GetIntOr("RBF_INTERVAL_SEC", 4);
  int max_bumps = ConfigManager::GetIntOr("RBF_MAX_BUMPS", 3);
  int receipt_timeout = ConfigManager::GetIntOr("RECEIPT_TIMEOUT_MS", 3000);
  bool use_private = ConfigManager::GetBoolOr("SUBMIT_PRIVATE", false);
  for (int i = 0; i <= max_bumps; ++i) {
    auto signed_tx = signer_.SignEip1559(base_tx);
    std::string tx_hash;
    if (use_private) {
      mev_.ApplyTxRandomizationDelay();
      tx_hash = rpc_.EthSendRawTransactionPrivate(signed_tx);
    } else {
      tx_hash = rpc_.EthSendRawTransactionPublic(signed_tx);
    }
    out_tx_hash = tx_hash;
    // Log tx_submitted for this attempt
    {
      auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
      std::string j = std::string("{") +
        "\"event\":\"tx_submitted\"," +
        "\"ts_ms\":" + std::to_string(now) + "," +
        "\"tx_hash\":\"" + tx_hash + "\"," +
        "\"nonce\":" + std::to_string(base_tx.nonce) + "," +
        "\"submit_kind\":\"" + std::string(use_private?"private":"public") + "\"," +
        "\"rbf_index\":" + std::to_string(i) + "," +
        "\"max_fee_per_gas\":" + std::to_string(base_tx.max_fee_per_gas) + "," +
        "\"max_priority_fee\":" + std::to_string(base_tx.max_priority_fee_per_gas) +
        "}";
      StructuredLogger::Instance().LogJsonLine(j);
    }
    if (WaitForReceipt(tx_hash, receipt_timeout)) return true;
    base_tx.max_fee_per_gas = static_cast<unsigned long long>(base_tx.max_fee_per_gas * bump);
    base_tx.max_priority_fee_per_gas = static_cast<unsigned long long>(base_tx.max_priority_fee_per_gas * bump);
    // Log bump
    {
      auto nowb = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
      std::string jb = std::string("{") +
        "\"event\":\"tx_rbf_bump\"," +
        "\"ts_ms\":" + std::to_string(nowb) + "," +
        "\"tx_hash_prev\":\"" + tx_hash + "\"," +
        "\"nonce\":" + std::to_string(base_tx.nonce) + "," +
        "\"bump_index\":" + std::to_string(i+1) + "," +
        "\"new_fees\":{\"max_fee\":" + std::to_string(base_tx.max_fee_per_gas) + ",\"max_prio\":" + std::to_string(base_tx.max_priority_fee_per_gas) + "}}";
      StructuredLogger::Instance().LogJsonLine(jb);
    }
    std::this_thread::sleep_for(std::chrono::seconds(interval));
  }
  return false;
}

