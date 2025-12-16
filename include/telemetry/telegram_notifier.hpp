#pragma once
#include <string>
#include <atomic>
#include <chrono>

struct ProfitReport {
  uint64_t attempted = 0;
  uint64_t completed = 0;
  long double total_profit_usdc = 0.0L;
};

class TelegramNotifier {
public:
  TelegramNotifier(const std::string& bot_token, const std::string& chat_id);
  void NotifyInstant(const std::string& text);
  void AccumulateAttempt(bool completed, long double profit_usdc);
  void MaybeSendHourlyReport();
  // Set once at process start
  static void SetHttpClient(class HttpClient* client);
private:
  std::string bot_token_;
  std::string chat_id_;
  std::atomic<uint64_t> attempted_{0};
  std::atomic<uint64_t> completed_{0};
  std::atomic<long long> profit_microusdc_{0}; // store in micro USDC to avoid fp contention
  std::chrono::system_clock::time_point last_report_;
  void SendMessage(const std::string& text);
};

