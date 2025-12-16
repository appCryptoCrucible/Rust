#include "encoding/rlp.hpp"
#include <vector>
#include <string>

namespace {
  std::vector<unsigned char> hexToBytes(const std::string& hex) {
    size_t start = (hex.rfind("0x", 0) == 0) ? 2 : 0;
    std::vector<unsigned char> out; out.reserve((hex.size() - start) / 2);
    for (size_t i = start; i + 1 < hex.size(); i += 2) {
      unsigned int byte = 0;
      char c1 = hex[i], c2 = hex[i+1];
      auto val = [](char c)->int{ if (c>='0'&&c<='9') return c-'0'; if (c>='a'&&c<='f') return 10+c-'a'; if (c>='A'&&c<='F') return 10+c-'A'; return 0; };
      byte = (val(c1) << 4) | val(c2);
      out.push_back(static_cast<unsigned char>(byte));
    }
    return out;
  }

  std::string bytesToHex0x(const std::vector<unsigned char>& data) {
    static const char* hex = "0123456789abcdef";
    std::string out; out.reserve(2 * data.size() + 2); out += "0x";
    for (unsigned char b : data) { out += hex[b >> 4]; out += hex[b & 0xF]; }
    return out;
  }

  void append(std::vector<unsigned char>& buf, const std::vector<unsigned char>& more) {
    buf.insert(buf.end(), more.begin(), more.end());
  }

  std::vector<unsigned char> encodeLength(size_t len, unsigned char offset) {
    if (len < 56) {
      return std::vector<unsigned char>{ static_cast<unsigned char>(offset + len) };
    }
    // long length
    std::vector<unsigned char> lenBytes;
    size_t tmp = len;
    while (tmp) { lenBytes.insert(lenBytes.begin(), static_cast<unsigned char>(tmp & 0xFF)); tmp >>= 8; }
    std::vector<unsigned char> out;
    out.push_back(static_cast<unsigned char>(offset + 55 + lenBytes.size()));
    append(out, lenBytes);
    return out;
  }
}

namespace RLP {
  std::string EncodeBytes(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> out;
    if (data.size() == 1 && data[0] < 0x80) {
      out.push_back(data[0]);
    } else {
      auto prefix = encodeLength(data.size(), 0x80);
      append(out, prefix);
      append(out, data);
    }
    return bytesToHex0x(out);
  }

  std::string EncodeString(const std::string& hex0x) {
    auto bytes = hexToBytes(hex0x);
    return EncodeBytes(bytes);
  }

  std::string EncodeUint(unsigned long long value) {
    if (value == 0) {
      // empty string encodes to 0x80; but for 0, RLP int is 0x80? Actually for 0, value is 0x80 with empty bytes
      std::vector<unsigned char> out{0x80};
      return bytesToHex0x(out);
    }
    std::vector<unsigned char> bytes;
    while (value) { bytes.insert(bytes.begin(), static_cast<unsigned char>(value & 0xFF)); value >>= 8; }
    return EncodeBytes(bytes);
  }

  std::string EncodeList(const std::vector<std::string>& elements) {
    std::vector<unsigned char> payload;
    for (const auto& e : elements) {
      auto b = hexToBytes(e);
      append(payload, b);
    }
    std::vector<unsigned char> out;
    auto prefix = encodeLength(payload.size(), 0xC0);
    append(out, prefix);
    append(out, payload);
    return bytesToHex0x(out);
  }
}


