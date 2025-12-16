#pragma once
#include <string>

namespace JsonRpcUtil {
  // Returns the "result" field as string (raw), throws on error
  std::string ExtractResult(const std::string& json_body);
  // Returns hex string field from result (e.g., baseFeePerGas), empty if not present
  std::string ExtractFieldHex(const std::string& json_body, const std::string& field_name);
  // Extract error message if present, empty otherwise
  std::string ExtractError(const std::string& json_body);
}


