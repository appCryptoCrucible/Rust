#include "wallet/signer.hpp"
#include "common/logger.hpp"
#include "crypto/keccak.hpp"
#include "crypto/secp256k1.hpp"
#include "encoding/rlp.hpp"
#include <stdexcept>
#include <vector>
#include <string>

static std::vector<unsigned char> HexToBytes(const std::string& hex) {
  size_t start = (hex.rfind("0x", 0) == 0) ? 2 : 0;
  std::vector<unsigned char> out; out.reserve((hex.size() - start) / 2);
  for (size_t i = start; i + 1 < hex.size(); i += 2) {
    unsigned char hi = (unsigned char)std::stoi(hex.substr(i,1), nullptr, 16);
    unsigned char lo = (unsigned char)std::stoi(hex.substr(i+1,1), nullptr, 16);
    out.push_back((hi << 4) | lo);
  }
  return out;
}

static std::string BytesToHex0x(const std::vector<unsigned char>& data) {
  static const char* hex = "0123456789abcdef";
  std::string out; out.reserve(2 * data.size() + 2); out += "0x";
  for (unsigned char b : data) { out += hex[b >> 4]; out += hex[b & 0xF]; }
  return out;
}

Signer::Signer(const std::string& private_key_hex) {
  if (private_key_hex.empty()) throw std::invalid_argument("empty private key");
  priv_ = HexToBytes(private_key_hex);
  if (priv_.size() != 32) throw std::invalid_argument("invalid private key length");
  try {
    auto pub = Crypto::PublicKeyFromPrivate(priv_);
    // keccak256 of pubkey[1:] (skip 0x04)
    std::string raw(reinterpret_cast<const char*>(&pub[1]), pub.size() - 1);
    auto hash = Crypto::Keccak256Raw(raw);
    // last 20 bytes
    std::string h = hash.rfind("0x", 0) == 0 ? hash.substr(2) : hash;
    if (h.size() < 40) address_ = "0x0000000000000000000000000000000000000000"; else address_ = "0x" + h.substr(h.size() - 40);
  } catch (...) {
    address_ = "0x0000000000000000000000000000000000000000";
  }
}

std::string Signer::SignEip1559(const TransactionFields& tx) {
  // RLP: [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList]
  std::vector<std::string> core{
    RLP::EncodeUint(static_cast<unsigned long long>(tx.chain_id)),
    RLP::EncodeUint(tx.nonce),
    RLP::EncodeUint(tx.max_priority_fee_per_gas),
    RLP::EncodeUint(tx.max_fee_per_gas),
    RLP::EncodeUint(tx.gas_limit),
    RLP::EncodeString(tx.to),
    RLP::EncodeUint(tx.value),
    RLP::EncodeString(tx.data),
    RLP::EncodeList({})
  };
  auto rlp_core = RLP::EncodeList(core);
  // sighash = keccak256(0x02 || rlp_core)
  std::string core_bytes; core_bytes.reserve(1 + (rlp_core.size() - 2)/2);
  core_bytes.push_back(static_cast<char>(0x02));
  // decode hex rlp_core into bytes
  {
    size_t start = 2; for (size_t i = start; i + 1 < rlp_core.size(); i += 2) {
      auto val = [](char c)->int{ if (c>='0'&&c<='9') return c-'0'; if (c>='a'&&c<='f') return 10+c-'a'; if (c>='A'&&c<='F') return 10+c-'A'; return 0; };
      unsigned char b = static_cast<unsigned char>((val(rlp_core[i]) << 4) | val(rlp_core[i+1]));
      core_bytes.push_back(static_cast<char>(b));
    }
  }
  auto digest_hex = Crypto::Keccak256Raw(core_bytes);
  // parse digest to bytes
  std::vector<unsigned char> digest;
  {
    std::string s = digest_hex.rfind("0x", 0) == 0 ? digest_hex.substr(2) : digest_hex;
    digest.reserve(32);
    auto val = [](char c)->int{ if (c>='0'&&c<='9') return c-'0'; if (c>='a'&&c<='f') return 10+c-'a'; if (c>='A'&&c<='F') return 10+c-'A'; return 0; };
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
      digest.push_back(static_cast<unsigned char>((val(s[i]) << 4) | val(s[i+1])));
    }
  }
  auto sig = Crypto::SignDigest(priv_, digest);
  // Append yParity, r, s
  std::vector<std::string> full = core;
  full.push_back(RLP::EncodeUint(sig.v - 27));
  // r,s as big endian unsigned integers
  auto toHex0x = [](const std::vector<unsigned char>& data){
    static const char* hex = "0123456789abcdef";
    std::string out; out.reserve(2 * data.size() + 2); out += "0x";
    bool leading = true;
    for (unsigned char b : data) {
      char hi = hex[b >> 4], lo = hex[b & 0xF];
      if (leading && hi == '0' && lo == '0') continue; else leading = false;
      out += hi; out += lo;
    }
    if (leading) out += '0';
    return out;
  };
  full.push_back(RLP::EncodeString(toHex0x(sig.r)));
  full.push_back(RLP::EncodeString(toHex0x(sig.s)));
  auto rlp_full = RLP::EncodeList(full);
  // typed tx: 0x02 || rlp_full
  return std::string("0x02") + rlp_full.substr(2);
}

std::string Signer::Address() const { return address_override_.empty() ? address_ : address_override_; }

void Signer::SetAddressOverride(const std::string& addr) { address_override_ = addr; }

