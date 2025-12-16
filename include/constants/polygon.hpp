#pragma once
#include <string>

namespace PolygonConstants {
  inline constexpr int CHAIN_ID = 137;
  // Aave v3 Pool (proxy)
  inline const std::string AAVE_V3_POOL = "0x794a61358D6845594F94dc1DB02A252b5b4814aD";
  // Common routers
  inline const std::string UNISWAP_V3_ROUTER = "0xE592427A0AEce92De3Edee1F18E0157C05861564"; // SwapRouter
  inline const std::string QUICKSWAP_ROUTER = "0xa5E0829CaCEd8fFDD4De3c43696c57F7D7A678ff"; // V2 router
  inline const std::string SUSHISWAP_ROUTER = "0x1b02da8cb0d097eb8d57a175b88c7d8b47997506"; // V2 router
  inline const std::string QUICKSWAP_FACTORY = "0x5757371414417b8c6caad45baef941abc7d3ab32"; // V2 factory
  inline const std::string SUSHISWAP_FACTORY = "0xc35DADB65012eC5796536bD9864eDe8773aBc74C4"; // V2 factory
  inline const std::string WMATIC = "0x0d500B1d8E8eF31E21C99d1Db9A6444d3ADf1270"; // Wrapped native
  inline const std::string USDC = "0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174";
  // Multicall (Multicall3 universal deployment)
  inline const std::string MULTICALL3 = "0xCA11bde05977b3631167028862bE2a173976CA11";
}

