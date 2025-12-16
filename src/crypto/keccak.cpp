#include "crypto/keccak.hpp"
#include <string>
#ifdef HAVE_CRYPTOPP
#include <cryptopp/keccak.h>
#endif

namespace Crypto {
  static std::string BytesToHex0x(const unsigned char* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out; out.reserve(len * 2 + 2); out += "0x";
    for (size_t i = 0; i < len; ++i) { unsigned char b = data[i]; out += hex[b >> 4]; out += hex[b & 0xF]; }
    return out;
  }

  std::string Keccak256Raw(const std::string& raw) {
#ifdef HAVE_CRYPTOPP
    CryptoPP::Keccak_256 hash;
    unsigned char digest[32];
    hash.CalculateTruncatedDigest(digest, sizeof(digest), reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
    return BytesToHex0x(digest, 32);
#else
    return "0x"; // not available
#endif
  }
  std::string Keccak256Hex(const std::string& hex_input) {
#ifdef HAVE_CRYPTOPP
    // parse hex
    size_t start = (hex_input.rfind("0x", 0) == 0) ? 2 : 0;
    std::string bytes; bytes.reserve((hex_input.size() - start) / 2);
    auto val = [](char c)->int{ if (c>='0'&&c<='9') return c-'0'; if (c>='a'&&c<='f') return 10+c-'a'; if (c>='A'&&c<='F') return 10+c-'A'; return 0; };
    for (size_t i = start; i + 1 < hex_input.size(); i += 2) {
      unsigned char b = static_cast<unsigned char>((val(hex_input[i]) << 4) | val(hex_input[i+1]));
      bytes.push_back(static_cast<char>(b));
    }
    return Keccak256Raw(bytes);
#else
    return "0x";
#endif
  }
}

