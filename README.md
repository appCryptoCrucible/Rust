Defi Liquidation Bot (Polygon, Aave v3)

Overview
- Hyper-optimized C++ bot scaffold targeting Polygon liquidations (Aave v3) funded via on-chain flash loans, with private relay submission (Nodies), custom MEV protection, Telegram ops telemetry, and modular routing/signing.

Status
- Scaffolding implemented: modules for RPC, MEV protection, Telegram, liquidation orchestration, routing, signer, RLP, keccak/secp stubs, nonce manager, gas strategy, thread pool, profit consolidation, and Solidity executor contract.
- Next: implement real crypto (keccak/secp256k1), ABI selector and encoding, Aave v3 scanning, and DEX routing.

Build (CMake)
1) Prereqs: CMake >= 3.20, C++20 compiler, libcurl (optional, enabled by default for HTTP).
2) Configure:
   - Windows (PowerShell):
     - Install libcurl (e.g., vcpkg: `vcpkg install curl[ssl]`), set toolchain if needed
     - `cmake -S . -B build -DUSE_LIBCURL=ON`
     - `cmake --build build --config Release`
   - Linux/macOS:
     - `cmake -S . -B build -DUSE_LIBCURL=ON`
     - `cmake --build build --config Release`

Run
1) Copy `.env.example` to `.env` and fill values.
2) Ensure `contracts/LiquidationExecutor.sol` is deployed; put its address in `EXECUTOR_ADDRESS`.
3) Run the binary from repo root:
   - Windows PowerShell:
     - `cd build/Release` (or `build` on non-MSVC)
     - `./defi_liquidation_bot`

Environment (.env)
- NODIES_RPC_URL=https://polygon.nodies.app/...
- NODIES_PRIVATE_TX_URL=...
- NODIES_AUTH_HEADER=Bearer ...
- TELEGRAM_BOT_TOKEN=...
- TELEGRAM_CHAT_ID=...
- PRIVATE_KEY=0x...
- EXECUTOR_ADDRESS=0x...
- MAX_SLIPPAGE_BPS=50
- MIN_LIQ_USD=100
- MAX_LIQ_USD=51000
- ALERT_TX_USD=15000

Security
- Secrets are loaded once at startup and cached in-memory.
- Use Nodies private tx relay for MEV protection when possible.

Notes
- Keccak/secp256k1/ABI encoding currently stubbed. Signing and selector implementation are next.
- Telegram hourly ops summaries and instant alerts for >= 15k liquidations are wired.

