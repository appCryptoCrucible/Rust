#include "config/network.hpp"
#include "common/config_manager.hpp"

NetworkConfig LoadNetworkConfig(bool dry_run) {
  NetworkConfig cfg;
  if (dry_run) {
    // DRY_RUN now targets a local mainnet fork (Hardhat/Anvil/Foundry) for end-to-end testing
    cfg.chain_id = ConfigManager::GetIntOr("FORK_CHAIN_ID", 137); // mirror Polygon mainnet by default
    cfg.rpc_url = ConfigManager::GetOrThrow("FORK_RPC_URL");
    if (auto a = ConfigManager::Get("FORK_AUTH_HEADER")) cfg.auth_header = *a;
    // Use fork-specific executor if provided, else fall back to mainnet one
    cfg.executor_address = ConfigManager::Get("FORK_EXECUTOR_ADDRESS").value_or(
      ConfigManager::Get("EXECUTOR_ADDRESS").value_or("")
    );
    // Subgraph remains the same endpoint used on mainnet to source data quickly
    cfg.aave_subgraph_url = ConfigManager::Get("AAVE_SUBGRAPH_URL").value_or("");
  } else {
    cfg.chain_id = 137;
    if (auto pub = ConfigManager::Get("PUBLIC_RPC_URL")) {
      cfg.rpc_url = *pub; // prefer public RPC if provided
    } else {
      cfg.rpc_url = ConfigManager::GetOrThrow("NODIES_RPC_URL");
      if (auto p = ConfigManager::Get("NODIES_PRIVATE_TX_URL")) cfg.private_tx_url = *p;
      if (auto a = ConfigManager::Get("NODIES_AUTH_HEADER")) cfg.auth_header = *a;
    }
    cfg.executor_address = ConfigManager::GetOrThrow("EXECUTOR_ADDRESS");
    cfg.aave_subgraph_url = ConfigManager::Get("AAVE_SUBGRAPH_URL").value_or("");
  }
  return cfg;
}

