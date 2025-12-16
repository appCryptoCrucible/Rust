#include "routing/reserves_cache.hpp"
#include "node_connection/rpc_client.hpp"
#include <sstream>
#include <algorithm>

static std::string No0x(const std::string& s){ return (s.rfind("0x",0)==0)?s.substr(2):s; }
static std::string Pad32(const std::string& h){ if (h.size()>=64) return h.substr(h.size()-64); return std::string(64-h.size(),'0')+h; }

std::string V2ReservesCache::KeyFactoryPair(const std::string& factory, const std::string& a, const std::string& b){
    std::string aa = a, bb = b; if (aa < bb) return factory + '|' + aa + '|' + bb; else return factory + '|' + bb + '|' + aa;
}

std::string V2ReservesCache::KeyReserves(const std::string& pair, unsigned long long block){
    std::ostringstream k; k << pair << '|' << block; return k.str();
}

const std::string& V2ReservesCache::GetPairAddress(RpcClient& rpc,
                                                   const std::string& factory,
                                                   const std::string& tokenA,
                                                   const std::string& tokenB){
    auto key = KeyFactoryPair(factory, tokenA, tokenB);
    auto it = pair_cache_.find(key);
    if (it != pair_cache_.end()) return it->second;
    // getPair(address,address) -> 0xe6a43905
    std::string data = std::string("0xe6a43905") + Pad32(No0x(tokenA)) + Pad32(No0x(tokenB));
    auto res = rpc.EthCall(factory, data, std::nullopt, 800);
    if (res.size() < 42) {
        pair_cache_[key] = std::string("0x");
    } else {
        std::string out = "0x" + No0x(res).substr(24, 40);
        pair_cache_[key] = out;
    }
    return pair_cache_[key];
}

std::pair<unsigned long long, unsigned long long> V2ReservesCache::GetReserves(RpcClient& rpc,
                                                                               const std::string& factory,
                                                                               const std::string& tokenA,
                                                                               const std::string& tokenB,
                                                                               unsigned long long current_block){
    const std::string& pair = GetPairAddress(rpc, factory, tokenA, tokenB);
    if (pair.size() < 42) return {0ULL,0ULL};
    auto rkey = KeyReserves(pair, current_block);
    auto it = reserves_cache_.find(rkey);
    if (it != reserves_cache_.end()) return {it->second.r0, it->second.r1};
    // getReserves() -> 0x0902f1ac returns (r0,r1,timestamp) as 3x uint112/uint112/uint32
    auto res = rpc.EthCall(pair, "0x0902f1ac", std::nullopt, 800);
    std::string hex = No0x(res);
    if (hex.size() < 64*3) return {0ULL,0ULL};
    auto parseU = [](const std::string& h){ try { return std::stoull(h, nullptr, 16);} catch (...) { return 0ULL; } };
    unsigned long long r0 = parseU(hex.substr(0,64));
    unsigned long long r1 = parseU(hex.substr(64,64));
    reserves_cache_[rkey] = {r0, r1, current_block};
    // Align to (tokenA, tokenB) ordering; Uniswap V2 defines token0 < token1 lexicographically by address
    std::string aa = tokenA, bb = tokenB; if (No0x(aa) <= No0x(bb)) return {r0,r1}; else return {r1,r0};
}

unsigned long long V2ReservesCache::QuoteV2Local(RpcClient& rpc,
                                                 const std::string& factory,
                                                 const std::string& tokenIn,
                                                 const std::string& tokenOut,
                                                 unsigned long long amount_in,
                                                 unsigned long long current_block){
    auto [reserve_in, reserve_out] = GetReserves(rpc, factory, tokenIn, tokenOut, current_block);
    if (reserve_in == 0ULL || reserve_out == 0ULL || amount_in == 0ULL) return 0ULL;
    // 0.3% fee
    unsigned long long amount_in_with_fee = static_cast<unsigned long long>((amount_in * 997ULL));
    unsigned long long numerator = (amount_in_with_fee * reserve_out);
    unsigned long long denominator = (reserve_in * 1000ULL) + amount_in_with_fee;
    if (denominator == 0ULL) return 0ULL;
    return numerator / denominator;
}



