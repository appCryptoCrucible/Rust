#include "node_connection/rpc_client.hpp"
#include "net/http_client.hpp"
#include "common/logger.hpp"
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include "utils/json_rpc.hpp"
static std::string ParseJsonResultString(const std::string& json) { return JsonRpcUtil::ExtractResult(json); }
static inline std::string Trim(const std::string& s) {
  size_t start = 0, end = s.size();
  while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
  while (end > start && std::isspace(static_cast<unsigned char>(s[end-1]))) --end;
  return s.substr(start, end - start);
}
static void ApplyAuthHeader(std::unordered_map<std::string, std::string>& headers,
                            const std::optional<std::string>& auth_header_opt) {
  if (!auth_header_opt) return;
  const std::string& raw = *auth_header_opt;
  auto pos = raw.find(':');
  if (pos != std::string::npos) {
    std::string name = Trim(raw.substr(0, pos));
    std::string value = Trim(raw.substr(pos + 1));
    if (!name.empty() && !value.empty()) {
      headers[name] = value;
      return;
    }
  }
  headers["Authorization"] = raw;
}

RpcClient::RpcClient(HttpClient& http,
                     const std::string& public_endpoint_url,
                     const std::optional<std::string>& auth_header,
                     const std::optional<std::string>& private_endpoint_url)
  : http_(http), public_endpoint_(public_endpoint_url), auth_header_(auth_header), private_endpoint_(private_endpoint_url) {
  default_headers_.reserve(2);
  default_headers_["Content-Type"] = "application/json";
  if (auth_header_) {
    ApplyAuthHeader(default_headers_, auth_header_);
  }
}

std::string RpcClient::BuildPayload(const std::string& method, const std::vector<std::string>& params) {
  static const std::string prefix = "{\"jsonrpc\":\"2.0\",\"method\":\"";
  static const std::string mid = "\",\"params\":[";
  static const std::string suffix = "],\"id\":1}";
  size_t total_params_len = 0; for (const auto& p : params) total_params_len += p.size();
  std::string out; out.reserve(prefix.size() + method.size() + mid.size() + total_params_len + params.size() + suffix.size());
  out += prefix; out += method; out += mid;
  for (size_t i = 0; i < params.size(); ++i) { out += params[i]; if (i + 1 < params.size()) out += ","; }
  out += suffix;
  return out;
}

std::string RpcClient::HttpPost(const std::string& url,
                                const std::string& body,
                                const std::unordered_map<std::string, std::string>& headers,
                                int timeout_ms) {
  auto resp = http_.Post(url, body, headers, timeout_ms);
  if (resp.status < 200 || resp.status >= 300) {
    Logger::Error("HTTP POST failed status=" + std::to_string(resp.status));
    throw std::runtime_error("HTTP POST failed");
  }
  return resp.body;
}

std::string RpcClient::Send(const std::string& json_payload, int timeout_ms) {
  return HttpPost(public_endpoint_, json_payload, default_headers_, timeout_ms);
}

std::string RpcClient::EthCall(const std::string& to, const std::string& data, const std::optional<std::string>& block, int timeout_ms) {
  std::vector<std::string> params;
  std::ostringstream call;
  call << "{\\\"to\\\":\\\"" << to << "\\\",\\\"data\\\":\\\"" << data << "\\\"}";
  params.push_back(call.str());
  if (block) params.emplace_back("\"" + *block + "\""); else params.emplace_back("\"latest\"");
  auto payload = BuildPayload("eth_call", params);
  auto resp = Send(payload, timeout_ms);
  return ParseJsonResultString(resp);
}

std::string RpcClient::EthSendRawTransactionPublic(const std::string& raw_tx_hex, int timeout_ms) {
  std::vector<std::string> params{ "\"" + raw_tx_hex + "\"" };
  auto payload = BuildPayload("eth_sendRawTransaction", params);
  auto resp = Send(payload, timeout_ms);
  return ParseJsonResultString(resp);
}

std::string RpcClient::EthSendRawTransactionPrivate(const std::string& raw_tx_hex, int timeout_ms) {
  if (!private_endpoint_) return EthSendRawTransactionPublic(raw_tx_hex, timeout_ms);
  auto headers = default_headers_;
  // Some private relays expect a different method name; default to public for now.
  std::vector<std::string> params{ "\"" + raw_tx_hex + "\"" };
  auto payload = BuildPayload("eth_sendRawTransaction", params);
  auto resp = HttpPost(*private_endpoint_, payload, headers, timeout_ms);
  return ParseJsonResultString(resp);
}

std::string RpcClient::EthGetBlockByNumber(const std::string& tag_or_hex, bool full_tx, int timeout_ms) {
  std::vector<std::string> params{ "\"" + tag_or_hex + "\"", full_tx ? "true" : "false" };
  auto payload = BuildPayload("eth_getBlockByNumber", params);
  return Send(payload, timeout_ms);
}

std::string RpcClient::EthBlockNumber(int timeout_ms) {
  std::vector<std::string> params{};
  auto payload = BuildPayload("eth_blockNumber", params);
  auto resp = Send(payload, timeout_ms);
  return ParseJsonResultString(resp);
}

std::string RpcClient::EthGetTransactionReceipt(const std::string& tx_hash, int timeout_ms) {
  std::vector<std::string> params{ "\"" + tx_hash + "\"" };
  auto payload = BuildPayload("eth_getTransactionReceipt", params);
  return Send(payload, timeout_ms);
}

std::string RpcClient::EthGetTransactionCount(const std::string& address, const std::string& block_tag, int timeout_ms) {
  std::vector<std::string> params{ "\"" + address + "\"", "\"" + block_tag + "\"" };
  auto payload = BuildPayload("eth_getTransactionCount", params);
  return Send(payload, timeout_ms);
}

std::string RpcClient::EthMaxPriorityFeePerGas(int timeout_ms) {
  std::vector<std::string> params{};
  auto payload = BuildPayload("eth_maxPriorityFeePerGas", params);
  return Send(payload, timeout_ms);
}

std::string RpcClient::EthNewBlockFilter(int timeout_ms) {
  std::vector<std::string> params{};
  auto payload = BuildPayload("eth_newBlockFilter", params);
  auto resp = Send(payload, timeout_ms);
  return ParseJsonResultString(resp);
}

std::string RpcClient::EthGetFilterChanges(const std::string& filter_id, int timeout_ms) {
  std::vector<std::string> params{ "\"" + filter_id + "\"" };
  auto payload = BuildPayload("eth_getFilterChanges", params);
  return Send(payload, timeout_ms);
}

std::string RpcClient::EthUninstallFilter(const std::string& filter_id, int timeout_ms) {
  std::vector<std::string> params{ "\"" + filter_id + "\"" };
  auto payload = BuildPayload("eth_uninstallFilter", params);
  auto resp = Send(payload, timeout_ms);
  return ParseJsonResultString(resp);
}

