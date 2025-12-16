#pragma once
#include <string>

namespace Crypto {
  // Returns 0x-prefixed hex keccak256 hash of the input interpreted as raw bytes
  std::string Keccak256Raw(const std::string& raw);
  // Returns 0x-prefixed hex keccak256 of hex-encoded input (0x-hex or hex)
  std::string Keccak256Hex(const std::string& hex_input);
}

