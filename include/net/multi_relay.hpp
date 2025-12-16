#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class HttpClient;

class MultiRelaySender {
public:
  MultiRelaySender(HttpClient& http,
                   const std::vector<std::string>& endpoints,
                   const std::vector<std::string>& auth_headers);
  // Returns tx hash on first success; throws on total failure
  std::string SendRawTransaction(const std::string& signed_tx_hex, int timeout_ms = 5000);
private:
  HttpClient& http_;
  std::vector<std::string> endpoints_;
  std::vector<std::string> auth_headers_; // either size 0/1 or same size as endpoints
};


