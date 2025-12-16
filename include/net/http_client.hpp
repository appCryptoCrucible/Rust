#pragma once
#include <string>
#include <unordered_map>

struct HttpResponse {
  long status = 0;
  std::string body;
};

class HttpClient {
public:
  virtual ~HttpClient() = default;
  virtual HttpResponse Post(const std::string& url,
                            const std::string& body,
                            const std::unordered_map<std::string, std::string>& headers,
                            int timeout_ms) = 0;
};

// Factory for a libcurl-based client. Returns nullptr if not available at build time.
HttpClient* CreateCurlHttpClient();

// Optional tuning knobs for persistent HTTP client behavior
struct HttpClientTuning {
  int num_handles = 2;           // small pool size for concurrent requests
  bool enable_http2 = true;      // try HTTP/2 when TLS is used
  bool enable_tcp_keepalive = true;
  int tcp_keepidle_s = 30;
  int tcp_keepintvl_s = 15;
};

// Create curl client with tuning options (may be ignored if not supported)
HttpClient* CreateCurlHttpClientTuned(const HttpClientTuning& tuning);

