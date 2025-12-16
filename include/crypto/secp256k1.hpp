#pragma once
#include <string>
#include <vector>

namespace Crypto {
  struct Signature { std::vector<unsigned char> r; std::vector<unsigned char> s; unsigned char v = 27; };
  // Sign 32-byte digest with secp256k1; private key is 32-byte raw
  Signature SignDigest(const std::vector<unsigned char>& priv32, const std::vector<unsigned char>& digest32);
  // Derive uncompressed public key (65 bytes, 0x04 || X(32) || Y(32)) from private key
  std::vector<unsigned char> PublicKeyFromPrivate(const std::vector<unsigned char>& priv32);
}

