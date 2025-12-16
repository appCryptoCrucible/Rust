#pragma once
#include <string>
#include <algorithm>

inline std::string Ensure0x(const std::string& in) {
  if (in.size() >= 2 && (in[0] == '0') && (in[1] == 'x' || in[1] == 'X')) return in;
  return std::string("0x") + in;
}

inline std::string Strip0x(const std::string& s) {
  if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) return s.substr(2);
  return s;
}

inline std::string ToLowerHex(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  return out;
}

