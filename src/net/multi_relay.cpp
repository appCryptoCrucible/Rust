#include "net/multi_relay.hpp"
#include "net/http_client.hpp"
#include <stdexcept>
#include <sstream>

static std::string BuildPayload(const std::string& raw) {
  std::ostringstream oss;
  oss << "{\"jsonrpc\":\"2.0\",\"method\":\"eth_sendRawTransaction\",\"params\":[\"" << raw << "\"],\"id\":1}";
  return oss.str();
}

MultiRelaySender::MultiRelaySender(HttpClient& http,
                                   const std::vector<std::string>& endpoints,
                                   const std::vector<std::string>& auth_headers)
  : http_(http), endpoints_(endpoints), auth_headers_(auth_headers) {}

std::string MultiRelaySender::SendRawTransaction(const std::string& signed_tx_hex, int timeout_ms) {
  std::string body = BuildPayload(signed_tx_hex);
  std::unordered_map<std::string, std::string> headers{{"Content-Type","application/json"}};
  for (size_t i = 0; i < endpoints_.size(); ++i) {
    if (!auth_headers_.empty()) headers["Authorization"] = (auth_headers_.size() == endpoints_.size() ? auth_headers_[i] : auth_headers_[0]);
    try {
      auto resp = http_.Post(endpoints_[i], body, headers, timeout_ms);
      if (resp.status >= 200 && resp.status < 300) return resp.body;
    } catch (...) {
      continue;
    }
  }
  throw std::runtime_error("All private relays failed");
}


