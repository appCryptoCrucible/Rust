#include "common/logger.hpp"
#include "common/config_manager.hpp"
#include "node_connection/rpc_client.hpp"
#include "net/http_client.hpp"
#include "mev/protection.hpp"
#include "telemetry/csv_logger.hpp"
#include "liquidation/liquidation_manager.hpp"
#include "routing/dex_router.hpp"
#include "wallet/signer.hpp"
#include "wallet/nonce_manager.hpp"
#include "gas/gas_strategy.hpp"
#include "profit/consolidator.hpp"
#include "scheduler/thread_pool.hpp"
#include "liquidation/executor_abi.hpp"
#include "config/network.hpp"
#include "liquidation/precompute_cache.hpp"
#include "scheduler/gas_escalator.hpp"
#include "net/multi_relay.hpp"
#include "telemetry/structured_logger.hpp"
#include "net/block_watcher.hpp"
#include "liquidation/hf_scanner.hpp"
#include "constants/polygon.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "scheduler/cpu_affinity.hpp"
#include <nlohmann/json.hpp>

int main() {
  try {
    std::cout << "=== Starting DeFi Liquidation Bot ===" << std::endl;
    std::cout << "Step 1: Initializing Logger..." << std::endl;
  Logger::Initialize("bot.log", LogLevel::INFO);
    std::cout << "Step 2: Initializing Structured Logger..." << std::endl;
  StructuredLogger::Instance().Initialize("metrics.jsonl");
    std::cout << "Step 3: Loading .env configuration..." << std::endl;
  ConfigManager::Initialize(".env");

  // Add debug output to see what's happening
  Logger::Info("Bot starting up...");
  Logger::Info("Logger initialized successfully");

  std::cout << "Step 4: Loading network configuration..." << std::endl;
  const bool dry_run = ConfigManager::GetBoolOr("DRY_RUN", true);
  std::cout << "DRY_RUN: " << (dry_run ? "true" : "false") << std::endl;
  
  NetworkConfig net = LoadNetworkConfig(dry_run);
  std::cout << "Network loaded. RPC: " << net.rpc_url << std::endl;
  
  std::cout << "Step 5: Setting up ABI selectors..." << std::endl;
  if (auto sel = ConfigManager::Get("EXECUTOR_LIQ_ARB_SELECTOR")) {
    ExecutorABI::SetLiquidateAndArbSelector(*sel);
  }
  if (auto selb = ConfigManager::Get("EXECUTOR_LIQ_BATCH_SELECTOR")) {
    ExecutorABI::SetLiquidateBatchSelector(*selb);
  }
  ExecutorABI::InitializeDefaultSelectors();

  std::cout << "Step 6: Setting up HTTP client..." << std::endl;
  HttpClientTuning http_tuning;
  http_tuning.enable_http2 = true;
  http_tuning.enable_tcp_keepalive = true;
  std::unique_ptr<HttpClient> http(CreateCurlHttpClientTuned(http_tuning));
  if (!http) { 
    std::cout << "ERROR: HTTP client not available (libcurl missing)" << std::endl;
    Logger::Critical("HTTP client not available (libcurl missing)"); 
    return 1; 
  }
  std::cout << "HTTP client created successfully" << std::endl;
  
  std::cout << "Step 7: Setting up RPC client..." << std::endl;
  RpcClient rpc(*http,
                net.rpc_url,
                net.auth_header,
                net.private_tx_url);
  std::cout << "Step 8: Setting up components..." << std::endl;
  DexRouterPlanner router;
  std::cout << "Router created" << std::endl;
  
  std::cout << "Step 9: Setting up signer..." << std::endl;
  Signer signer(ConfigManager::GetOrThrow("PRIVATE_KEY"));
  if (auto addr = ConfigManager::Get("WALLET_ADDRESS")) signer.SetAddressOverride(*addr);
  std::cout << "Signer created for address: " << signer.Address() << std::endl;
  std::cout << "Step 10: Setting up nonce manager..." << std::endl;
  NonceManager nonce(rpc, signer.Address());
  std::cout << "Nonce manager created" << std::endl;
  
  std::cout << "Step 11: Setting up gas strategy..." << std::endl;
  GasStrategy gas(rpc);
  std::cout << "Gas strategy created" << std::endl;
  
  std::cout << "Step 12: Setting up caches..." << std::endl;
  PrecomputeCache precompute_cache;
  GasEscalator escalator(1.2, std::chrono::seconds(4), 3);
  // Multi-relay private submission config
  std::vector<std::string> relay_urls; if (auto v = ConfigManager::Get("RELAY_URLS")) { std::istringstream iss(*v); std::string s; while (std::getline(iss, s, ',')) if(!s.empty()) relay_urls.push_back(s); }
  std::vector<std::string> relay_auths; if (auto v = ConfigManager::Get("RELAY_AUTH_HEADERS")) { std::istringstream iss(*v); std::string s; while (std::getline(iss, s, ',')) if(!s.empty()) relay_auths.push_back(s); }
  MultiRelaySender* multi_relay = nullptr; std::unique_ptr<MultiRelaySender> multi_relay_holder;
  // NOTE: Private relay submission available but disabled by default per operator request.
  // if (!relay_urls.empty()) { multi_relay_holder.reset(new MultiRelaySender(*http, relay_urls, relay_auths)); multi_relay = multi_relay_holder.get(); }
  MevProtectionConfig mev_cfg;
  mev_cfg.use_private_tx = true;
  mev_cfg.max_slippage_bps = ConfigManager::GetDoubleOr("MAX_SLIPPAGE_BPS", 50.0);
  MevProtector mev(mev_cfg);
  ProfitConsolidator consolidator(rpc, router, mev, signer, nonce, gas);
  std::cout << "Step 13: Setting up CSV logger..." << std::endl;
  CsvLogger csv_logger("liquidation_log.csv");
  Logger::Info("CSV Logger initialized successfully");
  std::cout << "CSV logger created" << std::endl;
  
  if (auto sel = ConfigManager::Get("EXECUTOR_LIQ_ARB_SELECTOR")) {
    ExecutorABI::SetLiquidateAndArbSelector(*sel);
  }
  
  std::cout << "Step 14: Creating LiquidationManager..." << std::endl;
  Logger::Info("Initializing LiquidationManager...");
  LiquidationManager manager(rpc, mev, csv_logger, router, signer, nonce, gas, net.executor_address, consolidator, http.get(), dry_run, precompute_cache, escalator, multi_relay);
  Logger::Info("LiquidationManager initialized successfully");
  std::cout << "LiquidationManager created successfully" << std::endl;

  const int max_concurrency = ConfigManager::GetIntOr("MAX_CONCURRENCY", 2);

  std::cout << "Step 15: Creating thread pool..." << std::endl;
  ThreadPool pool(max_concurrency > 0 ? static_cast<size_t>(max_concurrency) : 1);
  std::cout << "Thread pool created with " << max_concurrency << " threads" << std::endl;

  std::cout << "=== ALL COMPONENTS INITIALIZED SUCCESSFULLY ===" << std::endl;
  Logger::Info("=== DeFi Liquidation Bot Started ===");
  Logger::Info("Dry Run Mode: " + std::string(dry_run ? "ENABLED" : "DISABLED"));
  Logger::Info("RPC Endpoint: " + net.rpc_url);
  Logger::Info("Executor Address: " + net.executor_address);
  Logger::Info("Aave Subgraph: " + std::string(net.aave_subgraph_url.empty() ? "DISABLED" : "ENABLED"));
  Logger::Info("Starting main loop...");
  
  // Prepare HF scanner and monitored sets
  const std::string multicall_addr = ConfigManager::Get("MULTICALL_ADDRESS").value_or(PolygonConstants::MULTICALL3);
  const std::string aave_pool_env = ConfigManager::Get("AAVE_POOL").value_or(PolygonConstants::AAVE_V3_POOL);
  HFScanner hf_scanner(rpc, multicall_addr, aave_pool_env);

  auto parseCsv = [](const std::string& csv){ std::vector<std::string> v; std::string tmp; std::istringstream iss(csv); while (std::getline(iss, tmp, ',')) if(!tmp.empty()) v.push_back(tmp); return v; };
  const std::vector<std::string> monitor_users = parseCsv(ConfigManager::Get("MONITOR_USERS").value_or(""));
  const std::vector<std::string> debt_assets = [&]{
    auto s = ConfigManager::Get("DEBT_ASSETS").value_or(ConfigManager::Get("DEFAULT_DEBT_ASSET").value_or(""));
    return parseCsv(s);
  }();
  const std::vector<std::string> collat_assets = [&]{
    auto s = ConfigManager::Get("COLLATERAL_ASSETS").value_or(ConfigManager::Get("DEFAULT_COLLATERAL_ASSET").value_or(""));
    return parseCsv(s);
  }();

  std::cout << "=== STARTING BLOCK-DRIVEN LOOP ===" << std::endl;
  // Pin this main thread to a core for more consistent latency (optional)
  PinCurrentThreadToCore(0);

  // ---- Subgraph-backed watchlist manager (hysteresis + cap) ----
  // [DELETED SECTION] - All code from line 185 to 319 (fetchSubgraphCandidates, refreshWatchlistIfNeeded, and all related variables) will be removed.

  auto on_block = [&](unsigned long long bn){
    std::cout << "New block " << bn << ": scanning..." << std::endl;

    // Refresh watchlist periodically via subgraph (discovery/fallback), cap to WATCH_MAX_USERS
    // [DELETED] refreshWatchlistIfNeeded(bn);

    // Choose users to scan this block (on-chain). If watchlist empty, fall back to MONITOR_USERS.
    const std::vector<std::string>& users_to_scan = monitor_users;

    // Primary: real-time HF batching for selected users (on-chain Multicall latest)
    size_t precompute_cnt = 0;
    size_t monitored_liq = 0;
    if (!users_to_scan.empty()) {
      auto hfs = hf_scanner.FetchHealthFactors(users_to_scan);

      // Drive prestage/execute based on thresholds
      for (const auto& r : hfs) {
        if (r.hf < 1.05) {
          precompute_cnt++;
          for (const auto& d : debt_assets) {
            for (const auto& c : collat_assets) {
              if (d == c) continue;
              manager.PrecomputeCalldataFor(r.user, d, c);
            }
          }
        }
        if (r.hf < 1.0) {
          monitored_liq++;
          for (const auto& d : debt_assets) {
            for (const auto& c : collat_assets) {
              if (d == c) continue;
              LiquidationTarget t;
              t.user = r.user;
              t.debt_asset = d;
              t.collateral_asset = c;
              t.debt_amount = 0.0L;
              t.collateral_amount = 0.0L;
              t.usd_value = ConfigManager::GetDoubleOr("MIN_LIQ_USD", 100.0);
              auto* mgr_ptr = &manager; const double max_slip = mev_cfg.max_slippage_bps;
              pool.Enqueue([mgr_ptr, max_slip, t]{ (void)mgr_ptr->ExecuteAtomic(t, max_slip); });
            }
          }
        }
      }
    }

    std::cout << "Precompute HF<1.05: " << precompute_cnt << std::endl;
    std::cout << "Monitored HF<1: " << monitored_liq << std::endl;
    std::cout << "Eligible targets this block: 0" << std::endl; // Placeholder

    manager.ConsolidateProfitsToUSDC();
  };

  BlockWatcher watcher(rpc, on_block);
  std::cout << "Starting block watcher..." << std::endl;
  watcher.Start();
  std::cout << "Block watcher started successfully" << std::endl;
  // Keep process alive
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(60));

  Logger::Shutdown();
  StructuredLogger::Instance().Shutdown();
  std::cout << "Bot shutdown complete" << std::endl;
  return 0;
  } catch (const std::exception& e) {
    std::cout << "CRITICAL ERROR: " << e.what() << std::endl;
    std::cout << "Bot failed to start. Check configuration and try again." << std::endl;
    return 1;
  } catch (...) {
    std::cout << "CRITICAL ERROR: Unknown exception occurred" << std::endl;
    std::cout << "Bot failed to start. Check configuration and try again." << std::endl;
    return 1;
  }
}

