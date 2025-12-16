#include "gas/gas_strategy.hpp"
#include "node_connection/rpc_client.hpp"
#include "utils/json_rpc.hpp"
#include "telemetry/structured_logger.hpp"
#include <chrono>

static unsigned long long ParseHexResultULL(const std::string& json) {
  auto pos = json.find("\"result\"");
  if (pos == std::string::npos) return 0ULL;
  pos = json.find('"', pos + 8);
  if (pos == std::string::npos) return 0ULL;
  auto pos2 = json.find('"', pos + 1);
  if (pos2 == std::string::npos) return 0ULL;
  std::string val = json.substr(pos + 1, pos2 - pos - 1);
  if (val.rfind("0x", 0) == 0) val = val.substr(2);
  if (val.empty()) return 0ULL;
  return std::stoull(val, nullptr, 16);
}

GasQuote GasStrategy::Quote() {
  // Competitive gas: 2x base + priority bump
  unsigned long long prio = 30'000'000'000ULL; // default 30 gwei
  try {
    auto prio_json = rpc_.EthMaxPriorityFeePerGas();
    unsigned long long p = ParseHexResultULL(prio_json);
    if (p > 0) prio = p;
  } catch (...) {}
  unsigned long long base = 50'000'000'000ULL; // fallback base
  try {
    auto block_json = rpc_.EthGetBlockByNumber("latest", false);
    auto base_hex = JsonRpcUtil::ExtractFieldHex(block_json, "baseFeePerGas");
    unsigned long long b = 0ULL; if (!base_hex.empty()) b = std::stoull(base_hex.substr(2), nullptr, 16);
    if (b > 0) base = b;
  } catch (...) {}
  unsigned long long max_fee = base * 2 + prio; // aggressive inclusion
  // Emit structured telemetry (lightweight)
  {
    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    std::string j = std::string("{") +
      "\"event\":\"gas_quote\"," +
      "\"ts_ms\":" + std::to_string(now) + "," +
      "\"base_fee\":" + std::to_string(base) + "," +
      "\"priority_fee\":" + std::to_string(prio) + "," +
      "\"max_fee\":" + std::to_string(max_fee) +
      "}";
    StructuredLogger::Instance().LogJsonLine(j);
  }
  return GasQuote{ max_fee, prio };
}

