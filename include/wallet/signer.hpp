#pragma once
#include <string>
#include <vector>

struct TransactionFields {
  int chain_id = 137; // Polygon
  unsigned long long nonce = 0;
  unsigned long long gas_limit = 0;
  unsigned long long max_fee_per_gas = 0; // wei
  unsigned long long max_priority_fee_per_gas = 0; // wei
  std::string to; // 0x...
  unsigned long long value = 0; // wei
  std::string data; // 0x...
};

class Signer {
public:
  explicit Signer(const std::string& private_key_hex);
  std::string SignEip1559(const TransactionFields& tx);
  std::string Address() const;
  void SetAddressOverride(const std::string& addr);
private:
  std::vector<unsigned char> priv_;
  std::string address_;
  std::string address_override_;
};

