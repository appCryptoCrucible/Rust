#pragma once
#include <string>
#include <vector>

namespace RLP {
  std::string EncodeBytes(const std::vector<unsigned char>& data);
  std::string EncodeString(const std::string& hex0x);
  std::string EncodeUint(unsigned long long value);
  std::string EncodeList(const std::vector<std::string>& elements);
}

