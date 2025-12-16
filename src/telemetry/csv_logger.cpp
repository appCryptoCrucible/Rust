#include "telemetry/csv_logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <fstream>

CsvLogger::CsvLogger(const std::string& filename) : filename_(filename), last_flush_(std::chrono::steady_clock::now()) {
  write_buffer_.reserve(BUFFER_SIZE);
  // Check if file exists and has content
  std::ifstream check_file(filename);
  bool file_exists = check_file.good();
  bool has_headers = false;
  
  if (file_exists) {
    std::string first_line;
    if (std::getline(check_file, first_line)) {
      // Check if first line contains our expected header
      has_headers = (first_line.find("Timestamp") != std::string::npos && 
                     first_line.find("TX_Hash") != std::string::npos);
    }
    check_file.close();
  }
  
  // Open file in append mode
  file_.open(filename, std::ios::app);
  if (!file_.is_open()) {
    std::cerr << "Failed to open CSV log file: " << filename << std::endl;
    return;
  }
  
  // Only write header if file is new or doesn't have headers
  if (!file_exists || !has_headers) {
    WriteHeader();
    file_.flush(); // Ensure header is written
  }
}

CsvLogger::~CsvLogger() {
  if (file_.is_open()) {
    ForceFlush(); // Ensure all data is written
    file_.close();
  }
}

void CsvLogger::WriteHeader() {
  std::lock_guard<std::mutex> lock(mutex_);
  file_ << "Timestamp,TX_Hash,User_Address,Debt_Asset,Collateral_Asset,"
         << "Debt_Amount,Collateral_Amount,Debt_Amount_USD,Collateral_Amount_USD,"
         << "Liquidation_Premium,Gas_Cost_Wei,Gas_Cost_USD,Profit_USDC,Profit_EUR,"
         << "Execution_Status,Chain_ID,Executor_Address,Gas_Strategy,MEV_Protection,"
         << "RPC_Endpoint,Dry_Run" << std::endl;
}

void CsvLogger::WriteRecord(const LiquidationRecord& record) {
  // Pre-format the record string for maximum speed
  std::ostringstream oss;
  oss << "\"" << record.timestamp << "\","
      << "\"" << record.tx_hash << "\","
      << "\"" << record.user_address << "\","
      << "\"" << record.debt_asset << "\","
      << "\"" << record.collateral_asset << "\","
      << std::fixed << std::setprecision(18) << record.debt_amount << ","
      << std::fixed << std::setprecision(18) << record.collateral_amount << ","
      << std::fixed << std::setprecision(2) << record.debt_amount_usd << ","
      << std::fixed << std::setprecision(2) << record.collateral_amount_usd << ","
      << std::fixed << std::setprecision(2) << record.liquidation_premium << ","
      << record.gas_cost_wei << ","
      << std::fixed << std::setprecision(2) << record.gas_cost_usd << ","
      << std::fixed << std::setprecision(2) << record.profit_usdc << ","
      << std::fixed << std::setprecision(2) << record.profit_eur << ","
      << "\"" << record.execution_status << "\","
      << "\"" << record.chain_id << "\","
      << "\"" << record.executor_address << "\","
      << "\"" << record.gas_strategy << "\","
      << "\"" << record.mev_protection << "\","
      << "\"" << record.rpc_endpoint << "\","
      << (record.dry_run ? "true" : "false") << std::endl;
  
  WriteToBuffer(oss.str());
}

void CsvLogger::LogLiquidationAttempt(const LiquidationRecord& record) {
  LiquidationRecord attempt_record = record;
  attempt_record.execution_status = "ATTEMPT";
  attempt_record.timestamp = GetCurrentTimestamp();
  WriteRecord(attempt_record);
  // No individual flush - uses buffer system
}

void CsvLogger::LogLiquidationSuccess(const LiquidationRecord& record) {
  LiquidationRecord success_record = record;
  success_record.execution_status = "SUCCESS";
  success_record.timestamp = GetCurrentTimestamp();
  WriteRecord(success_record);
  // No individual flush - uses buffer system
}

void CsvLogger::LogLiquidationFailure(const LiquidationRecord& record, const std::string& reason) {
  LiquidationRecord failure_record = record;
  failure_record.execution_status = "FAILED: " + reason;
  failure_record.timestamp = GetCurrentTimestamp();
  WriteRecord(failure_record);
  // No individual flush - uses buffer system
}

void CsvLogger::LogGasStrategy(const std::string& strategy, long double gas_price_gwei, long double gas_price_usd) {
  std::ostringstream oss;
  oss << "\"" << GetCurrentTimestamp() << "\",GAS_STRATEGY,\"\",\"\",\"\","
      << "0,0,0,0,0," << gas_price_gwei << "," << std::fixed << std::setprecision(2) << gas_price_usd
      << ",0,0,\"GAS_UPDATE\",137,\"\",\"" << strategy << "\",\"\",\"\",false" << std::endl;
  WriteToBuffer(oss.str());
}

void CsvLogger::LogProfitConsolidation(const std::string& tx_hash, long double amount_usdc, long double amount_eur) {
  std::ostringstream oss;
  oss << "\"" << GetCurrentTimestamp() << "\",\"" << tx_hash << "\",\"\",\"\",\"\","
      << "0,0,0,0,0,0,0," << std::fixed << std::setprecision(2) << amount_usdc << ","
      << std::fixed << std::setprecision(2) << amount_eur << ",\"CONSOLIDATION\",137,\"\",\"\",\"\",false" << std::endl;
  WriteToBuffer(oss.str());
}

void CsvLogger::LogHourlySummary(long double total_profit_usdc, long double total_profit_eur, int attempts, int successes) {
  std::ostringstream oss;
  oss << "\"" << GetCurrentTimestamp() << "\",HOURLY_SUMMARY,\"\",\"\",\"\","
      << "0,0,0,0,0,0,0," << std::fixed << std::setprecision(2) << total_profit_usdc << ","
      << std::fixed << std::setprecision(2) << total_profit_eur << ",\"HOURLY_SUMMARY\",137,\"\",\"\",\"\",false" << std::endl;
  WriteToBuffer(oss.str());
}

void CsvLogger::WriteToBuffer(const std::string& record) {
  std::lock_guard<std::mutex> lock(mutex_);
  write_buffer_.push_back(record);
  
  // Flush if buffer is full or time interval reached
  if (write_buffer_.size() >= BUFFER_SIZE || 
      std::chrono::steady_clock::now() - last_flush_ >= FLUSH_INTERVAL) {
    FlushBuffer();
  }
}

void CsvLogger::FlushBuffer() {
  if (write_buffer_.empty()) return;
  
  // Write all buffered records at once
  for (const auto& record : write_buffer_) {
    file_ << record;
  }
  
  file_.flush();
  write_buffer_.clear();
  last_flush_ = std::chrono::steady_clock::now();
}

void CsvLogger::Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  FlushBuffer();
}

void CsvLogger::ForceFlush() {
  Flush(); // Immediate flush for critical records
}

std::string CsvLogger::GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  
  std::stringstream ss;
  ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
  ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
  ss << " UTC";
  
  return ss.str();
}

long double CsvLogger::ConvertUsdToEur(long double usd_amount) {
  // Simple conversion - in production you'd want to fetch live rates
  // For German tax purposes, you might want to use specific exchange rates
  const long double usd_to_eur_rate = 0.85; // Approximate rate
  return usd_amount * usd_to_eur_rate;
}
