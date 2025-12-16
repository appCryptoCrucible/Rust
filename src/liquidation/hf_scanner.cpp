#include "liquidation/hf_scanner.hpp"
#include "node_connection/rpc_client.hpp"
#include <sstream>
#include <nlohmann/json.hpp>
#include <algorithm>

// Minimal ABI encoding for multicall aggregate((target,callData)[])
// For speed, this uses fast string ops; in production, switch to binary buffers.
static std::string Pad32(const std::string& h) {
    if (h.size() >= 64) return h.substr(h.size()-64);
    return std::string(64 - h.size(), '0') + h;
}
static std::string No0x(const std::string& s) { return (s.rfind("0x",0)==0)?s.substr(2):s; }

std::vector<HFResult> HFScanner::FetchHealthFactors(const std::vector<std::string>& users) {
    std::vector<HFResult> out;
    if (users.empty()) return out;
    out.reserve(users.size());
    // Build calls for getUserAccountData(address)
    static const std::string selector = "b6b55f25"; // keccak("getUserAccountData(address)")
    if (users.size() == 1) {
        std::string calldata;
        calldata.reserve(2 + 8 + 64);
        calldata += "0x";
        calldata += selector;
        calldata += Pad32(No0x(users[0]));
        try {
            const std::string r = rpc_.EthCall(aave_pool_, calldata, std::nullopt, 800);
            const std::string hex = No0x(r);
            if (hex.size() >= 64 * 6) {
                const std::string hf_hex = hex.substr(64 * 5, 64);
                unsigned long long hf_hi = 0ULL;
                try { hf_hi = std::stoull(hf_hex, nullptr, 16); } catch (...) { hf_hi = 0ULL; }
                const double hf = static_cast<double>(hf_hi) / 1e18;
                out.push_back({users[0], hf});
            }
        } catch (...) {}
        return out;
    }
    // Attempt on-chain Multicall3 tryAggregate(false, calls)
    try {
        // Helpers
        auto hexLenBytes = [](const std::string& hexNo0x){ return static_cast<unsigned long long>(hexNo0x.size() / 2); };
        auto pad32 = [](const std::string& h){ if (h.size() >= 64) return h.substr(h.size()-64); return std::string(64 - h.size(), '0') + h; };
        auto encodeBool = [&](bool v){ return std::string(63, '0') + (v ? '1' : '0'); };
        auto encodeAddress = [&](const std::string& addr){ return std::string(24, '0') + No0x(addr); };
        auto toHex = [](unsigned long long v){ std::ostringstream ss; ss << std::hex << std::nouppercase << v; std::string s = ss.str(); if (s.size() % 2 != 0) s = "0" + s; for (auto& c : s) c = static_cast<char>(std::tolower(c)); return s; };
        auto encodeUint = [&](unsigned long long v){ return pad32(toHex(v)); };

        // Prebuild inner call datas
        std::vector<std::string> datas; datas.reserve(users.size());
        for (const auto& u : users) {
            std::string d; d.reserve(2 + 8 + 64);
            d += "0x"; d += selector; d += Pad32(No0x(u));
            datas.push_back(No0x(d));
        }
        // Head (bool requireSuccess, offset to calls)
        // tryAggregate selector
        const std::string tryAggSel = "252dba42";
        std::string enc; enc.reserve(8 + 64 + 64 + 64 + (users.size()* (64+64)) + (users.size()* (64 + 64 + 64*2)));
        enc += tryAggSel;
        // head
        enc += encodeBool(false);           // requireSuccess
        enc += encodeUint(0x40ULL);         // offset to calls array (64 bytes)
        // tail: calls array
        // base pointer of array data begins immediately here
        const unsigned long long n = static_cast<unsigned long long>(users.size());
        enc += encodeUint(n);               // length
        // Reserve space for tuple heads (address, offset)
        // We'll compute per-element tail offsets
        // First compute total head size of the array elements
        const unsigned long long tupleHeadBytes = 64ULL + 64ULL; // address + offset
        const unsigned long long headsBytes = n * tupleHeadBytes;
        // Base offset to first tuple tail, relative to start of array data
        unsigned long long runningTailOffset = 32ULL + headsBytes; // 32 for length + heads
        // Write tuple heads
        for (size_t i = 0; i < users.size(); ++i) {
            enc += encodeAddress(aave_pool_);
            enc += encodeUint(runningTailOffset);
            // Each tail will be 32 (length) + padded data
            const unsigned long long lenBytes = hexLenBytes(datas[i]);
            const unsigned long long padded = ((lenBytes + 31ULL) / 32ULL) * 32ULL;
            runningTailOffset += 32ULL + padded;
        }
        // Write tuple tails (bytes)
        for (size_t i = 0; i < users.size(); ++i) {
            const std::string& dhex = datas[i];
            const unsigned long long lenBytes = hexLenBytes(dhex);
            enc += encodeUint(lenBytes);
            // data padded to 32-byte boundary
            enc += dhex;
            const unsigned long long rem = (32ULL - (lenBytes % 32ULL)) % 32ULL;
            if (rem > 0) enc += std::string(static_cast<size_t>(rem*2ULL), '0');
        }
        // Full calldata
        const std::string fullData = std::string("0x") + enc;
        const std::string mcResult = rpc_.EthCall(multicall_, fullData, std::nullopt, 900);
        const std::string res = No0x(mcResult);
        if (res.size() >= 64) {
            // Parse return: offset to array at [0:64]
            auto hexToULL = [&](const std::string& h){ unsigned long long v=0ULL; try{ v = std::stoull(h, nullptr, 16);}catch(...){ v=0ULL;} return v; };
            const unsigned long long arrOff = hexToULL(res.substr(0,64));
            const unsigned long long arrPos = arrOff * 2ULL;
            if (res.size() >= arrPos + 64) {
                const unsigned long long m = hexToULL(res.substr(static_cast<size_t>(arrPos),64));
                const unsigned long long headsStart = arrPos + 64ULL;
                // Guard
                const unsigned long long headStrideHex = 64ULL*2ULL; // 64 bytes -> 128 hex chars per field
                const unsigned long long perElemHeadHex = (64ULL+64ULL)*2ULL; // success + offset = 128 bytes -> 256 hex chars
                // Collect return datas
                std::vector<std::string> rets; rets.resize(static_cast<size_t>(std::min<unsigned long long>(m, users.size())));
                for (unsigned long long i = 0; i < std::min<unsigned long long>(m, users.size()); ++i) {
                    const unsigned long long headPos = headsStart + i * perElemHeadHex;
                    if (res.size() < headPos + (64ULL+64ULL)*2ULL) break;
                    // success ignored (pos headPos)
                    const unsigned long long offBytes = hexToULL(res.substr(static_cast<size_t>(headPos + 64ULL*2ULL), 64));
                    const unsigned long long elemTailPos = arrPos + offBytes*2ULL;
                    if (res.size() < elemTailPos + 64ULL) break;
                    const unsigned long long rlen = hexToULL(res.substr(static_cast<size_t>(elemTailPos), 64));
                    const unsigned long long rdataPos = elemTailPos + 64ULL;
                    const unsigned long long rhexChars = rlen * 2ULL;
                    if (res.size() < rdataPos + rhexChars) break;
                    rets[static_cast<size_t>(i)] = res.substr(static_cast<size_t>(rdataPos), static_cast<size_t>(rhexChars));
                }
                // Decode HF from each return
                for (size_t i = 0; i < rets.size(); ++i) {
                    const std::string& hex = rets[i];
                    if (hex.size() >= 64 * 6) {
                        const std::string hf_hex = hex.substr(64 * 5, 64);
                        unsigned long long hf_hi = 0ULL;
                        try { hf_hi = std::stoull(hf_hex, nullptr, 16); } catch (...) { hf_hi = 0ULL; }
                        const double hf = static_cast<double>(hf_hi) / 1e18;
                        out.push_back({users[i], hf});
                    } else {
                        out.push_back({users[i], 0.0});
                    }
                }
                return out;
            }
        }
    } catch (...) {
        // Multicall failed, fall through to JSON-RPC batch
    }

    // JSON-RPC batch fallback
    std::string payload;
    payload.reserve(users.size() * 160);
    payload += "[";
    for (size_t i = 0; i < users.size(); ++i) {
        const std::string& u = users[i];
        std::string data;
        data.reserve(2 + 8 + 64);
        data += "0x";
        data += selector;
        data += Pad32(No0x(u));
        payload += "{\"jsonrpc\":\"2.0\",\"id\":\"";
        payload += std::to_string(i);
        payload += "\",\"method\":\"eth_call\",\"params\":[{\"to\":\"";
        payload += aave_pool_;
        payload += "\",\"data\":\"";
        payload += data;
        payload += "\"},\"latest\"]}";
        if (i + 1 < users.size()) payload += ",";
    }
    payload += "]";
    try {
        const std::string resp = rpc_.Send(payload, 900);
        auto j = nlohmann::json::parse(resp, nullptr, false);
        if (!j.is_array()) return out;
        std::vector<double> hfs(users.size(), 0.0);
        for (const auto& el : j) {
            if (!el.contains("id") || !el.contains("result")) continue;
            const std::string id = el["id"].get<std::string>();
            size_t idx = 0; try { idx = static_cast<size_t>(std::stoul(id)); } catch (...) { continue; }
            if (idx >= users.size()) continue;
            const std::string result = el["result"].is_string() ? el["result"].get<std::string>() : std::string();
            const std::string hex = No0x(result);
            if (hex.size() >= 64 * 6) {
                const std::string hf_hex = hex.substr(64 * 5, 64);
                unsigned long long hf_hi = 0ULL;
                try { hf_hi = std::stoull(hf_hex, nullptr, 16); } catch (...) { hf_hi = 0ULL; }
                hfs[idx] = static_cast<double>(hf_hi) / 1e18;
            }
        }
        for (size_t i = 0; i < users.size(); ++i) out.push_back({users[i], hfs[i]});
    } catch (...) { /* ignore batch errors */ }
    return out;
}


