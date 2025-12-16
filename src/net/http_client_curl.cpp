#include "net/http_client.hpp"
#include "common/logger.hpp"

#ifdef USE_LIBCURL
#include <curl/curl.h>
#endif

namespace {
#ifdef USE_LIBCURL
size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* s = static_cast<std::string*>(userdata);
  s->append(ptr, size * nmemb);
  return size * nmemb;
}
#endif
}

class CurlHttpClient : public HttpClient {
public:
  CurlHttpClient() {
#ifdef USE_LIBCURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
  }
  ~CurlHttpClient() override {
#ifdef USE_LIBCURL
    curl_global_cleanup();
#endif
  }
  HttpResponse Post(const std::string& url,
                    const std::string& body,
                    const std::unordered_map<std::string, std::string>& headers,
                    int timeout_ms) override {
    HttpResponse resp;
#ifdef USE_LIBCURL
    CURL* curl = curl_easy_init();
    if (!curl) { Logger::Error("curl_easy_init failed"); return resp; }
    std::string response_string;
    struct curl_slist* header_list = nullptr;
    for (const auto& kv : headers) {
      std::string line = kv.first + ": " + kv.second;
      header_list = curl_slist_append(header_list, line.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    // Disable SSL verification to avoid certificate issues
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
      Logger::Error(std::string("curl_easy_perform failed: ") + curl_easy_strerror(rc));
    } else {
      long code = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
      resp.status = code;
      resp.body = std::move(response_string);
    }
    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
#else
    (void)url; (void)body; (void)headers; (void)timeout_ms;
    Logger::Error("libcurl not available. Rebuild with -DUSE_LIBCURL");
#endif
    return resp;
  }
};

HttpClient* CreateCurlHttpClient() {
#ifdef USE_LIBCURL
  return new CurlHttpClient();
#else
  return nullptr;
#endif
}

class CurlHttpClientTuned : public CurlHttpClient {
public:
  explicit CurlHttpClientTuned(const HttpClientTuning& tuning) : tuning_(tuning) {}
  HttpResponse Post(const std::string& url,
                    const std::string& body,
                    const std::unordered_map<std::string, std::string>& headers,
                    int timeout_ms) override {
    HttpResponse resp;
#ifdef USE_LIBCURL
    CURL* curl = curl_easy_init();
    if (!curl) { Logger::Error("curl_easy_init failed"); return resp; }
    std::string response_string;
    struct curl_slist* header_list = nullptr;
    for (const auto& kv : headers) {
      std::string line = kv.first + ": " + kv.second;
      header_list = curl_slist_append(header_list, line.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    // Keep-alive and HTTP/2
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, tuning_.enable_tcp_keepalive ? 1L : 0L);
#ifdef CURLOPT_TCP_KEEPIDLE
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, tuning_.tcp_keepidle_s);
#endif
#ifdef CURLOPT_TCP_KEEPINTVL
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, tuning_.tcp_keepintvl_s);
#endif
#ifdef CURL_HTTP_VERSION_2TLS
    if (tuning_.enable_http2) curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
    // Disable SSL verification to avoid certificate issues
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
      Logger::Error(std::string("curl_easy_perform failed: ") + curl_easy_strerror(rc));
    } else {
      long code = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
      resp.status = code;
      resp.body = std::move(response_string);
    }
    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
#endif
    return resp;
  }
private:
  HttpClientTuning tuning_;
};

HttpClient* CreateCurlHttpClientTuned(const HttpClientTuning& tuning) {
#ifdef USE_LIBCURL
  return new CurlHttpClientTuned(tuning);
#else
  (void)tuning; return nullptr;
#endif
}

