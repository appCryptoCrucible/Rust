#include "telemetry/telegram_notifier.hpp"
#include "net/http_client.hpp"
#include "common/logger.hpp"
#include <sstream>
#include <unordered_map>

static std::string BuildTelegramUrl(const std::string& token) {
  // Check if we should use HTTP instead of HTTPS
  const char* use_https = std::getenv("TELEGRAM_USE_HTTPS");
  std::string base_url = (use_https && std::string(use_https) == "false") 
    ? "http://api.telegram.org" 
    : "https://api.telegram.org";
  return base_url + "/bot" + token + "/sendMessage";
}

static HttpClient*& GlobalHttpClient() {
  static HttpClient* client = nullptr;
  return client;
}

void TelegramNotifier::SetHttpClient(HttpClient* client) {
  GlobalHttpClient() = client;
}

TelegramNotifier::TelegramNotifier(const std::string& bot_token, const std::string& chat_id)
  : bot_token_(bot_token), chat_id_(chat_id), last_report_(std::chrono::system_clock::now()) {}

void TelegramNotifier::SendMessage(const std::string& text) {
  auto* http = GlobalHttpClient();
  if (!http) { Logger::Warning("No HTTP client set for Telegram; logging only"); Logger::Info("[Telegram] " + text); return; }
  std::unordered_map<std::string, std::string> headers{{"Content-Type","application/json"}};
  std::ostringstream body;
  body << "{\"chat_id\":\"" << chat_id_ << "\",\"text\":\"";
  for (char c : text) {
    if (c == '"') body << "\\\""; else if (c == '\n') body << "\\n"; else body << c;
  }
  body << "\"}";
  try {
    auto resp = http->Post(BuildTelegramUrl(bot_token_), body.str(), headers, 3000);
    if (resp.status < 200 || resp.status >= 300) {
      Logger::Warning("Telegram send failed status=" + std::to_string(resp.status));
    }
  } catch (const std::exception& ex) {
    Logger::Warning(std::string("Telegram send exception: ") + ex.what());
  }
}

void TelegramNotifier::NotifyInstant(const std::string& text) {
  SendMessage(text);
}

void TelegramNotifier::AccumulateAttempt(bool completed, long double profit_usdc) {
  attempted_.fetch_add(1, std::memory_order_relaxed);
  if (completed) completed_.fetch_add(1, std::memory_order_relaxed);
  long long micro = static_cast<long long>(profit_usdc * 1'000'000.0L);
  profit_microusdc_.fetch_add(micro, std::memory_order_relaxed);
}

void TelegramNotifier::MaybeSendHourlyReport() {
  auto now = std::chrono::system_clock::now();
  if (now - last_report_ < std::chrono::hours(1)) return;
  last_report_ = now;
  long double profit = static_cast<long double>(profit_microusdc_.load()) / 1'000'000.0L;
  std::ostringstream oss;
  oss << "Hourly report: attempted=" << attempted_.load()
      << ", completed=" << completed_.load()
      << ", profit USDC=" << profit;
  SendMessage(oss.str());
}

