cmake_minimum_required(VERSION 3.20)
project(DefiLiquidationBot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(include)

find_package(Threads REQUIRED)

option(USE_LIBCURL "Enable libcurl HTTP client" ON)
if(USE_LIBCURL)
  find_package(CURL REQUIRED)
  add_definitions(-DUSE_LIBCURL)
endif()

# Optional crypto libraries (installed via vcpkg)
option(USE_SECP256K1 "Enable secp256k1 for signing" ON)
option(USE_CRYPTOPP "Enable Crypto++ for Keccak" ON)
if(USE_SECP256K1)
  find_package(secp256k1 CONFIG QUIET)
  if(secp256k1_FOUND)
    add_definitions(-DHAVE_SECP256K1)
  endif()
endif()
if(USE_CRYPTOPP)
  find_package(cryptopp CONFIG QUIET)
  if(cryptopp_FOUND)
    add_definitions(-DHAVE_CRYPTOPP)
  endif()
endif()

# JSON
find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  # Header-only; rely on vcpkg include path via toolchain. No link target needed.
endif()
# If not found via config, fall back to vcpkg include path only usage

add_executable(defi_liquidation_bot
    src/main.cpp
    src/common/config_manager.cpp
    src/common/logger.cpp
    src/config/network.cpp
    src/crypto/keccak.cpp
    src/crypto/secp256k1.cpp
    src/encoding/rlp.cpp
    src/gas/gas_strategy.cpp
    src/liquidation/executor_abi.cpp
    src/liquidation/hf_scanner.cpp
    src/liquidation/liquidation_manager.cpp
    src/liquidation/precompute_cache.cpp
    src/mev/protection.cpp
    src/net/block_watcher.cpp
    src/net/http_client_curl.cpp
    src/net/ws_client.cpp
    src/node_connection/rpc_client.cpp
    src/oracle/price_oracle.cpp
    src/oracle/reserve_params.cpp
    src/profit/consolidator.cpp
    src/protocols/aave_v3.cpp
    src/protocols/erc20.cpp
    src/routing/dex_router.cpp
    src/routing/reserves_cache.cpp
    src/scheduler/thread_pool.cpp
    src/telemetry/csv_logger.cpp
    src/telemetry/structured_logger.cpp
    src/utils/json_rpc.cpp
    src/wallet/nonce_manager.cpp
    src/wallet/signer.cpp
    src/cache/decimals_cache.cpp
)

target_link_libraries(defi_liquidation_bot
  Threads::Threads
  $<$<BOOL:${USE_LIBCURL}>:CURL::libcurl>
  $<$<BOOL:${secp256k1_FOUND}>:secp256k1::secp256k1>
  $<$<BOOL:${cryptopp_FOUND}>:cryptopp::cryptopp>
  $<$<TARGET_EXISTS:nlohmann_json::nlohmann_json>:nlohmann_json::nlohmann_json>
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(defi_liquidation_bot PRIVATE -Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(defi_liquidation_bot PRIVATE /W4)
endif()
