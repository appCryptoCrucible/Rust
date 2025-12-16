#include "liquidation/executor_abi.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "crypto/keccak.hpp"

namespace {
  std::vector<unsigned char> hexToBytes(const std::string& hex) {
    size_t start = (hex.rfind("0x", 0) == 0) ? 2 : 0;
    std::vector<unsigned char> out; out.reserve((hex.size() - start) / 2);
    for (size_t i = start; i + 1 < hex.size(); i += 2) {
      unsigned int byte = 0;
      std::stringstream ss; ss << std::hex << hex.substr(i, 2); ss >> byte;
      out.push_back(static_cast<unsigned char>(byte));
    }
    return out;
  }

  std::string bytesToHex(const std::vector<unsigned char>& data) {
    static const char* hex = "0123456789abcdef";
    std::string out; out.reserve(2 * data.size());
    for (unsigned char b : data) { out += hex[b >> 4]; out += hex[b & 0xF]; }
    return out;
  }

  void append(std::vector<unsigned char>& buf, const std::vector<unsigned char>& more) {
    buf.insert(buf.end(), more.begin(), more.end());
  }

  std::vector<unsigned char> pad32(const std::vector<unsigned char>& in) {
    std::vector<unsigned char> out(32, 0);
    if (in.size() > 32) {
      // take last 32
      std::copy(in.end() - 32, in.end(), out.begin());
    } else {
      std::copy(in.begin(), in.end(), out.begin() + (32 - in.size()));
    }
    return out;
  }

  std::vector<unsigned char> encodeUint256(unsigned long long v) {
    std::vector<unsigned char> tmp;
    bool started = false;
    for (int i = 7; i >= 0; --i) {
      unsigned char byte = static_cast<unsigned char>((v >> (i * 8)) & 0xFFULL);
      if (!started && byte == 0 && i > 0) continue;
      started = true;
      tmp.push_back(byte);
    }
    if (tmp.empty()) tmp.push_back(0);
    return pad32(tmp);
  }

  std::vector<unsigned char> encodeAddress(const std::string& addr) {
    auto raw = hexToBytes(addr);
    // ensure 20 bytes
    if (raw.size() >= 20) raw = std::vector<unsigned char>(raw.end() - 20, raw.end());
    return pad32(raw);
  }

  std::vector<unsigned char> encodeBytesDynamic(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> out;
    // length
    auto len = encodeUint256(static_cast<unsigned long long>(data.size()));
    append(out, len);
    // data padded to 32
    std::vector<unsigned char> padded = data;
    size_t pad = (32 - (padded.size() % 32)) % 32;
    padded.insert(padded.end(), pad, 0);
    append(out, padded);
    return out;
  }
}

namespace ExecutorABI {
  static std::string g_selector = std::string(4, '\0');
  static std::string g_batch_selector = std::string(4, '\0');
  const std::string& GetLiquidateAndArbSelector() { return g_selector; }
  void SetLiquidateAndArbSelector(const std::string& selector0x) {
    // expects 0x + 8 hex chars (4 bytes)
    g_selector = selector0x;
    if (g_selector.size() == 8) g_selector = std::string("0x") + g_selector; // allow non-0x input
  }
  const std::string& GetLiquidateBatchSelector() { return g_batch_selector; }
  void SetLiquidateBatchSelector(const std::string& selector0x) {
    g_batch_selector = selector0x;
    if (g_batch_selector.size() == 8) g_batch_selector = std::string("0x") + g_batch_selector;
  }

  static std::string SelectorOf(const std::string& sig) {
    auto hash = Crypto::Keccak256Raw(sig);
    std::string h = hash.rfind("0x", 0) == 0 ? hash.substr(2) : hash;
    return std::string("0x") + h.substr(0, 8);
  }

  void InitializeDefaultSelectors() {
    if (g_selector.size() < 10) {
      // liquidateAndArb((address,address,uint256,address,(address,bytes)[],address,uint256))
      SetLiquidateAndArbSelector(SelectorOf("liquidateAndArb((address,address,uint256,address,(address,bytes)[],address,uint256))"));
    }
    if (g_batch_selector.size() < 10) {
      SetLiquidateBatchSelector(SelectorOf("liquidateBatchAndArb((address[],address,uint256[],address,(address,bytes)[],address,uint256))"));
    }
  }
  std::string BuildLiquidateAndArbCalldata(const Params& p) {
    // Function selector
    std::vector<unsigned char> out;
    // parse 0x........ into 4 bytes
    std::string sel = GetLiquidateAndArbSelector();
    if (sel.size() >= 10 && sel.rfind("0x", 0) == 0) {
      auto s = hexToBytes(sel);
      out.insert(out.end(), s.begin(), s.end());
    } else {
      out.insert(out.end(), 4, 0);
    }

    // Head (7 slots) and tail
    std::vector<unsigned char> head;
    std::vector<unsigned char> tail;

    // user (address)
    append(head, encodeAddress(p.user));
    // debtAsset (address)
    append(head, encodeAddress(p.debtAsset));
    // debtToCover (uint256)
    append(head, encodeUint256(p.debtToCover));
    // collateralAsset (address)
    append(head, encodeAddress(p.collateralAsset));
    // swaps (dynamic) -> offset
    unsigned long long head_size = 32ULL * 7ULL;
    append(head, encodeUint256(head_size));
    // profitReceiver (address)
    append(head, encodeAddress(p.profitReceiver));
    // minProfit (uint256)
    append(head, encodeUint256(p.minProfit));

    // Tail: encode Swap[]
    std::vector<unsigned char> swaps_enc;
    // length
    swaps_enc = encodeUint256(static_cast<unsigned long long>(p.swaps.size()));
    // For scaffolding, only support empty or routers with pre-encoded bytes
    if (!p.swaps.empty()) {
      // Build per-element heads then tails. Here we implement minimal: encode empty for safety.
      // Production should properly encode (router, bytes)
    }
    append(tail, swaps_enc);

    append(out, head);
    append(out, tail);

    // Return 0x + selector + encoded body
    std::string hex;
    hex.reserve(out.size() * 2 + 2);
    hex += "0x";
    hex += bytesToHex(out);
    return hex;
  }

  std::string BuildLiquidateBatchAndArbCalldata(const BatchParams& p) {
    std::vector<unsigned char> out;
    std::string sel = GetLiquidateBatchSelector();
    if (sel.size() >= 10 && sel.rfind("0x", 0) == 0) {
      auto s = hexToBytes(sel);
      out.insert(out.end(), s.begin(), s.end());
    } else {
      out.insert(out.end(), 4, 0);
    }
    // Encode tuple (address[],address,uint256[],address,(address,bytes)[],address,uint256)
    std::vector<unsigned char> head;
    std::vector<unsigned char> tail;
    // Offsets start after 7 slots
    unsigned long long head_size = 32ULL * 7ULL;
    // users (dynamic)
    append(head, encodeUint256(head_size));
    // debtAsset
    append(head, encodeAddress(p.debtAsset));
    // Prepare users array encoding
    std::vector<unsigned char> users_enc;
    append(users_enc, encodeUint256(static_cast<unsigned long long>(p.users.size())));
    for (const auto& u : p.users) append(users_enc, encodeAddress(u));
    // debtToCover array encoding
    std::vector<unsigned char> cover_enc;
    append(cover_enc, encodeUint256(static_cast<unsigned long long>(p.debtToCover.size())));
    for (auto v : p.debtToCover) append(cover_enc, encodeUint256(v));
    // collateralAsset
    append(head, encodeAddress(p.collateralAsset));
    // swaps (dynamic): dynamic array of tuples
    std::vector<unsigned char> swaps_enc;
    append(swaps_enc, encodeUint256(static_cast<unsigned long long>(p.swaps.size())));
    for (const auto& sw : p.swaps) {
      // Encode tuple(address router, bytes callData):
      // head: router, offset=0x40 (64) because two head slots
      append(swaps_enc, encodeAddress(sw.router));
      append(swaps_enc, encodeUint256(64));
      // tail: bytes dynamic
      auto bytes_vec = hexToBytes(sw.callDataHex);
      // length
      append(swaps_enc, encodeUint256(static_cast<unsigned long long>(bytes_vec.size())));
      // data padded
      std::vector<unsigned char> padded = bytes_vec; size_t pad = (32 - (padded.size() % 32)) % 32; padded.insert(padded.end(), pad, 0);
      append(swaps_enc, padded);
    }
    // profitReceiver
    append(head, encodeAddress(p.profitReceiver));
    // minProfit
    append(head, encodeUint256(p.minProfit));
    // Now set offsets for users and debtToCover and swaps
    // head layout: users(off), debtAsset, debtToCover(off), collateral, swaps(off), profitReceiver, minProfit
    // Build tail in order: users_enc, cover_enc, swaps_enc
    std::vector<unsigned char> full_tail;
    unsigned long long users_offset = head_size;
    unsigned long long cover_offset = head_size + static_cast<unsigned long long>(users_enc.size());
    unsigned long long swaps_offset = cover_offset + static_cast<unsigned long long>(cover_enc.size());
    // Rebuild head with correct offsets
    std::vector<unsigned char> final_head;
    append(final_head, encodeUint256(users_offset));
    // debtAsset
    append(final_head, encodeAddress(p.debtAsset));
    append(final_head, encodeUint256(cover_offset));
    append(final_head, encodeAddress(p.collateralAsset));
    append(final_head, encodeUint256(swaps_offset));
    append(final_head, encodeAddress(p.profitReceiver));
    append(final_head, encodeUint256(p.minProfit));
    // Build full payload
    append(full_tail, users_enc);
    append(full_tail, cover_enc);
    append(full_tail, swaps_enc);
    append(out, final_head);
    append(out, full_tail);
    std::string hex; hex.reserve(out.size() * 2 + 2); hex += "0x"; hex += bytesToHex(out); return hex;
  }
}

