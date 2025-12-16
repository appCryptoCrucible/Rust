#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <vector>

struct LiquidationRecord {
  std::string timestamp;
  std::string tx_hash;
  std::string user_address;
  std::string debt_asset;
  std::string collateral_asset;
  long double debt_amount;
  long double collateral_amount;
  long double debt_amount_usd;
  long double collateral_amount_usd;
  long double liquidation_premium;
  long double gas_cost_wei;
  long double gas_cost_usd;
  long double profit_usdc;
  long double profit_eur;
  std::string execution_status;
  std::string chain_id;
  std::string executor_address;
  std::string gas_strategy;
  std::string mev_protection;
  std::string rpc_endpoint;
  bool dry_run;
};

class CsvLogger {
public:
  explicit CsvLogger(const std::string& filename);
  ~CsvLogger();
  
  // Log liquidation attempt
  void LogLiquidationAttempt(const LiquidationRecord& record);
  
  // Log liquidation success
  void LogLiquidationSuccess(const LiquidationRecord& record);
  
  // Log liquidation failure
  void LogLiquidationFailure(const LiquidationRecord& record, const std::string& reason);
  
  // Log gas strategy update
  void LogGasStrategy(const std::string& strategy, long double gas_price_gwei, long double gas_price_usd);
  
  // Log profit consolidation
  void LogProfitConsolidation(const std::string& tx_hash, long double amount_usdc, long double amount_eur);
  
  // Log hourly summary
  void LogHourlySummary(long double total_profit_usdc, long double total_profit_eur, int attempts, int successes);
  
  // Flush data to disk
  void Flush();
  
  // Force immediate flush (for critical records)
  void ForceFlush();
  
private:
  std::ofstream file_;
  std::mutex mutex_;
  std::string filename_;
  
  // Performance optimizations
  std::vector<std::string> write_buffer_;
  static constexpr size_t BUFFER_SIZE = 100; // Batch 100 records before flush
  std::chrono::steady_clock::time_point last_flush_;
  static constexpr auto FLUSH_INTERVAL = std::chrono::seconds(5); // Flush every 5 seconds
  
  void WriteHeader();
  void WriteRecord(const LiquidationRecord& record);
  void WriteToBuffer(const std::string& record);
  void FlushBuffer();
  std::string GetCurrentTimestamp();
  long double ConvertUsdToEur(long double usd_amount);
};
