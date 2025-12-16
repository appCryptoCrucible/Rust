#pragma once
#include <string>
#include <unordered_map>
#include <utility>

class RpcClient;

// Lightweight V2 reserves cache keyed per block to enable local getAmountsOut math
class V2ReservesCache {
public:
    // Returns pair address for factory/tokenA/tokenB (order-independent). Caches results.
    const std::string& GetPairAddress(RpcClient& rpc,
                                      const std::string& factory,
                                      const std::string& tokenA,
                                      const std::string& tokenB);

    // Returns (reserveA, reserveB) aligned to (tokenA, tokenB) for this block. Refreshes once per block.
    std::pair<unsigned long long, unsigned long long> GetReserves(RpcClient& rpc,
                                                                  const std::string& factory,
                                                                  const std::string& tokenA,
                                                                  const std::string& tokenB,
                                                                  unsigned long long current_block);

    // Compute local V2 quote using constant product and 0.3% fee. Returns 0 if no pair/reserves.
    unsigned long long QuoteV2Local(RpcClient& rpc,
                                    const std::string& factory,
                                    const std::string& tokenIn,
                                    const std::string& tokenOut,
                                    unsigned long long amount_in,
                                    unsigned long long current_block);
private:
    struct ResEntry { unsigned long long r0 = 0ULL; unsigned long long r1 = 0ULL; unsigned long long block = 0ULL; };
    std::unordered_map<std::string, std::string> pair_cache_; // key: factory|a|b => pair
    std::unordered_map<std::string, ResEntry> reserves_cache_; // key: pair|block
    static std::string KeyFactoryPair(const std::string& factory, const std::string& a, const std::string& b);
    static std::string KeyReserves(const std::string& pair, unsigned long long block);
};



