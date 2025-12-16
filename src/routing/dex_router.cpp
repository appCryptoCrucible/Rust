#include "routing/dex_router.hpp"
#include "node_connection/rpc_client.hpp"
#include "constants/polygon.hpp"
#include <sstream>
#include <string>

RoutePlan DexRouterPlanner::PlanBest(const std::string& token_in,
                                     const std::string& token_out,
                                     long double amount_in,
                                     double max_slippage_bps) {
  (void)token_in; (void)token_out; (void)amount_in; (void)max_slippage_bps;
  // TODO: Query pool reserves and simulate multi-hop across Uniswap v3 / Quickswap / Sushiswap
  RoutePlan rp;
  rp.expected_price_impact_bps = 10.0; // placeholder
  return rp;
}

static std::string pad64(const std::string& hexNo0x) {
  std::string s = hexNo0x;
  if (s.size() > 64) return s.substr(s.size() - 64);
  if (s.size() < 64) s = std::string(64 - s.size(), '0') + s;
  return s;
}

std::string DexRouterPlanner::BuildV2SwapExactTokensCall(
    unsigned long long amount_in,
    unsigned long long amount_out_min,
    const std::vector<std::string>& path,
    const std::string& to,
    unsigned long long deadline) {
  // function selector 0x38ed1739
  std::ostringstream oss;
  oss << "0x38ed1739";
  // amountIn
  std::stringstream s1; s1 << std::hex << amount_in; oss << pad64(s1.str());
  // amountOutMin
  std::stringstream s2; s2 << std::hex << amount_out_min; oss << pad64(s2.str());
  // path offset (dynamic) -> head is 5 slots, so offset = 5*32 = 160 bytes
  oss << pad64("a0");
  // to address (20 bytes right aligned)
  std::string toNo0x = to.size() > 2 && to[0]=='0' && (to[1]=='x'||to[1]=='X') ? to.substr(2) : to;
  oss << pad64(toNo0x);
  // deadline
  std::stringstream s3; s3 << std::hex << deadline; oss << pad64(s3.str());
  // path encoding: length + items (each 32-bytes right-aligned)
  std::ostringstream tail;
  std::stringstream slen; slen << std::hex << path.size(); tail << pad64(slen.str());
  for (const auto& p : path) {
    std::string pNo0x = (p.size()>=2 && p[0]=='0' && (p[1]=='x'||p[1]=='X')) ? p.substr(2) : p;
    tail << pad64(pNo0x);
  }
  // pad tail to 32-byte boundary (already multiple of 32)
  oss << tail.str();
  return oss.str();
}

static std::string pad32(const std::string& hexNo0x) {
  std::string s = hexNo0x;
  if (!s.empty() && s.rfind("0x", 0) == 0) s = s.substr(2);
  if (s.size() > 64) return s.substr(s.size() - 64);
  if (s.size() < 64) s = std::string(64 - s.size(), '0') + s;
  return s;
}

static std::string EncodeGetAmountsOut(unsigned long long amount_in, const std::vector<std::string>& path) {
  // selector keccak("getAmountsOut(uint256,address[])") = 0xd06ca61f
  std::ostringstream oss;
  oss << "0xd06ca61f";
  std::stringstream s1; s1 << std::hex << amount_in; oss << pad32(s1.str());
  // path offset = 0x40 (2 slots after method head: amount + offset)
  oss << pad32("40");
  // dynamic path: length + addresses
  std::ostringstream tail;
  std::stringstream slen; slen << std::hex << path.size(); tail << pad32(slen.str());
  for (const auto& p : path) {
    std::string pNo0x = (p.size()>=2 && p[0]=='0' && (p[1]=='x'||p[1]=='X')) ? p.substr(2) : p;
    tail << pad32(pNo0x);
  }
  oss << tail.str();
  return oss.str();
}

unsigned long long DexRouterPlanner::QuoteV2GetAmountsOut(RpcClient& rpc,
                                                          const std::string& router,
                                                          const std::vector<std::string>& path,
                                                          unsigned long long amount_in) {
  try {
    auto data = EncodeGetAmountsOut(amount_in, path);
    auto res = rpc.EthCall(router, data, std::nullopt, 800);
    std::string r = (res.rfind("0x",0)==0?res.substr(2):res);
    // decode dynamic uint[]; last element is our out amount
    if (r.size() < 64*2) return 0ULL;
    // first slot is offset, skip to array head
    // For simplicity, parse from the end: last 32 bytes represent last amount (works for small paths)
    std::string last = r.substr(r.size() - 64, 64);
    return std::stoull(last, nullptr, 16);
  } catch (...) { return 0ULL; }
}

static inline std::string KeyForQuote(const std::string& router,
                                      const std::vector<std::string>& path,
                                      unsigned long long amount_in,
                                      unsigned long long block) {
  std::ostringstream k; k << router << '|';
  for (auto& p : path) { k << p << '>'; }
  k << amount_in << '#' << block; return k.str();
}

unsigned long long DexRouterPlanner::QuoteV2GetAmountsOutCached(RpcClient& rpc,
                                                                const std::string& router,
                                                                const std::vector<std::string>& path,
                                                                unsigned long long amount_in,
                                                                unsigned long long block_number) {
  static thread_local std::unordered_map<std::string, unsigned long long> cache;
  static thread_local unsigned long long last_block = 0ULL;
  if (block_number != last_block) { cache.clear(); last_block = block_number; }
  auto key = KeyForQuote(router, path, amount_in, block_number);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  auto val = QuoteV2GetAmountsOut(rpc, router, path, amount_in);
  cache.emplace(key, val);
  return val;
}

RoutePlan DexRouterPlanner::PlanBestSplitV2(RpcClient& rpc,
                                            const std::string& token_in,
                                            const std::string& token_out,
                                            unsigned long long amount_in_units) {
  RoutePlan rp;
  std::vector<std::string> path{ token_in, token_out };
  // Try splits: 100/0, 75/25, 50/50, 25/75, 0/100
  struct Split { int a; int b; } splits[] = {{100,0},{75,25},{50,50},{25,75},{0,100}};
  unsigned long long best_out = 0ULL; Split best{100,0};
  for (auto s : splits) {
    unsigned long long in_a = amount_in_units * s.a / 100;
    unsigned long long in_b = amount_in_units - in_a;
    unsigned long long out_a = in_a ? QuoteV2GetAmountsOut(rpc, PolygonConstants::QUICKSWAP_ROUTER, path, in_a) : 0ULL;
    unsigned long long out_b = in_b ? QuoteV2GetAmountsOut(rpc, PolygonConstants::SUSHISWAP_ROUTER, path, in_b) : 0ULL;
    unsigned long long total = out_a + out_b;
    if (total > best_out) { best_out = total; best = s; }
  }
  rp.legs.clear();
  rp.expected_price_impact_bps = 0.0;
  if (best.a > 0) rp.legs.push_back({PolygonConstants::QUICKSWAP_ROUTER, token_in, token_out, best.a / 100.0});
  if (best.b > 0) rp.legs.push_back({PolygonConstants::SUSHISWAP_ROUTER, token_in, token_out, best.b / 100.0});
  return rp;
}

