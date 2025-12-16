#pragma once
#include <string>
#include <optional>

struct NetworkConfig {
  int chain_id = 137; // default Polygon mainnet
  std::string rpc_url;
  std::optional<std::string> private_tx_url;
  std::optional<std::string> auth_header;
  std::string executor_address;
  std::string aave_subgraph_url;
};

// Loads network configuration from .env keys.
// If dry_run is true, prefer TESTNET variables and default chain_id to 80002 (Polygon Amoy).
NetworkConfig LoadNetworkConfig(bool dry_run);

