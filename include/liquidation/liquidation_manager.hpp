#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_set>

struct LiquidationTarget {
  std::string user;
  std::string debt_asset; // e.g., USDC
  std::string collateral_asset; // e.g., WETH
  long double debt_amount; // in debt units
  long double collateral_amount; // in collateral units
  long double usd_value; // approx
};

struct ExecutionResult {
  bool submitted = false;
  bool success = false;
  std::string tx_hash;
  long double profit_usdc = 0.0L;
};

class RpcClient;
class MevProtector;
class CsvLogger;
class DexRouterPlanner;
class Signer;
class NonceManager;
class GasStrategy;
class ProfitConsolidator;
class HttpClient;
class PrecomputeCache;
class GasEscalator;
class MultiRelaySender;
class Watchlist;

class LiquidationManager {
public:
  LiquidationManager(RpcClient& rpc,
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
                     MultiRelaySender* multi_relay);
  // Scans mempool/chain for eligible liquidations within configured value range
  std::vector<LiquidationTarget> ScanEligible(double min_usd, double max_usd);
  // Pre-stage candidates and cache calldata/routes; adapt buffer if enabled
  void TickPrestage(double min_usd, double max_usd);
  // Collect triggers (HF < 1.0) and execute immediately
  void CollectAndExecuteTriggers(double max_slippage_bps);
  // Builds a single-transaction flash-loan atomic liquidation bundle
  std::string BuildSignedAtomicLiquidationTx(const LiquidationTarget& t, double max_slippage_bps);
  // Submits via MEV protection/private relay
  ExecutionResult ExecuteAtomic(const LiquidationTarget& t, double max_slippage_bps);
  // After success, consolidate profits to USDC in a separate tx
  std::optional<std::string> ConsolidateProfitsToUSDC();
  // Build and execute batch for small same-pairing tickets
  void TryExecuteBatch(double max_slippage_bps);
  // Computational limits and target capping
  void ConfigureLimits(int max_targets_per_tick,
                       double filter_min_usd_sim,
                       double filter_preferred_max_usd);
  // Precompute and cache calldata for a (user, debt, collateral) tuple
  void PrecomputeCalldataFor(const std::string& user,
                             const std::string& debt_asset,
                             const std::string& collateral_asset);
  // Update watchlist from HF results and pre-stage entries near-liquidation
  void UpsertWatchFromHf(const std::vector<struct HFResult>& hfs,
                         const std::vector<std::string>& debt_assets,
                         const std::vector<std::string>& collat_assets,
                         double default_buffer);
private:
  RpcClient& rpc_;
  MevProtector& mev_;
  CsvLogger& logger_;
  DexRouterPlanner& router_;
  Signer& signer_;
  std::string executor_address_;
  NonceManager& nonce_manager_;
  GasStrategy& gas_strategy_;
  ProfitConsolidator& consolidator_;
  HttpClient* http_;
  bool dry_run_ = true;
  PrecomputeCache& cache_;
  GasEscalator& escalator_;
  MultiRelaySender* multi_relay_ = nullptr;
  bool prefer_private_submit_ = false;
  // Watchlist config/state
  double watch_buffer_current_ = 0.06;
  double watch_buffer_min_ = 0.03;
  double watch_buffer_max_ = 0.10;
  bool adaptive_watch_ = true;
  int watch_max_prestage_ = 100;
  std::unordered_set<std::string> volatile_assets_;
  int last_prestage_count_ = 0;
  Watchlist* watchlist_ = nullptr; // owned elsewhere or internal
  // Batch config
  bool batch_enabled_ = false;
  int batch_pair_min_count_ = 3;
  int batch_max_users_ = 10;
  double batch_max_total_usd_ = 25000.0;
  double batch_per_user_max_usd_ = 5000.0;
  double batch_slippage_bps_ = 60.0;
  // Limits
  int max_targets_per_tick_ = 50;
  double filter_min_usd_sim_ = 1000.0;
  double filter_preferred_max_usd_ = 15000.0;

  // Helpers (implementation detail)
  bool BuildAtomicTxFields(const LiquidationTarget& t, double max_slippage_bps, struct TransactionFields& out_tx);
  bool WaitForReceipt(const std::string& tx_hash, int timeout_ms);
  bool SubmitWithRbf(struct TransactionFields base_tx, std::string& out_tx_hash);
};

