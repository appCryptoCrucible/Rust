#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

class HttpClient;

struct JsonRpcRequest {
  std::string method;
  std::vector<std::string> params;
  std::string id;
};

class RpcClient {
public:
  RpcClient(HttpClient& http,
            const std::string& public_endpoint_url,
            const std::optional<std::string>& auth_header = std::nullopt,
            const std::optional<std::string>& private_endpoint_url = std::nullopt);
  // Sends raw JSON-RPC payload to public endpoint.
  std::string Send(const std::string& json_payload, int timeout_ms = 300);

  // Convenience helpers for common calls we will need fast.
  std::string EthCall(const std::string& to, const std::string& data, const std::optional<std::string>& block = std::nullopt, int timeout_ms = 300);
  std::string EthSendRawTransactionPublic(const std::string& raw_tx_hex, int timeout_ms = 5000);
  std::string EthSendRawTransactionPrivate(const std::string& raw_tx_hex, int timeout_ms = 5000);
  std::string EthGetBlockByNumber(const std::string& tag_or_hex, bool full_tx = false, int timeout_ms = 300);
  std::string EthBlockNumber(int timeout_ms = 300);
  std::string EthGetTransactionReceipt(const std::string& tx_hash, int timeout_ms = 5000);
  std::string EthGetTransactionCount(const std::string& address, const std::string& block_tag = "pending", int timeout_ms = 300);
  std::string EthMaxPriorityFeePerGas(int timeout_ms = 300);

  // Filters (HTTP pseudo-WebSocket):
  // Returns filter id (hex string) for new block filter
  std::string EthNewBlockFilter(int timeout_ms = 300);
  // Returns JSON array (as raw string) of block hashes since last poll
  std::string EthGetFilterChanges(const std::string& filter_id, int timeout_ms = 300);
  // Uninstall filter, returns boolean result as string
  std::string EthUninstallFilter(const std::string& filter_id, int timeout_ms = 300);

  const std::string& PublicEndpoint() const { return public_endpoint_; }
  const std::optional<std::string>& PrivateEndpoint() const { return private_endpoint_; }
private:
  HttpClient& http_;
  std::string public_endpoint_;
  std::optional<std::string> auth_header_;
  std::optional<std::string> private_endpoint_;
  std::unordered_map<std::string, std::string> default_headers_;
  std::string BuildPayload(const std::string& method, const std::vector<std::string>& params);
  std::string HttpPost(const std::string& url,
                       const std::string& body,
                       const std::unordered_map<std::string, std::string>& headers,
                       int timeout_ms);
};

