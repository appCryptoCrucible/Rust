#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time
from pathlib import Path

import requests
from eth_account import Account
from eth_account.signers.local import LocalAccount
from solcx import compile_standard, install_solc
from dotenv import load_dotenv

DEFAULT_SOLC_VERSION = "0.8.19"
CONTRACT_PATH = Path(__file__).resolve().parent.parent / "contracts" / "LiquidationExecutor.sol"
CONTRACT_NAME = "LiquidationExecutor"
MAINNET_AAVE_POOL = "0x794a61358D6845594F94dc1DB02A252b5b4814aD"
DEFAULT_MAINNET_RPC = "https://lb.nodies.app/v2/polygon/e3f9f54e-2085-4d7b-b093-56f632ff6517"


def load_contract_source() -> str:
    if not CONTRACT_PATH.exists():
        print(f"Contract source not found at {CONTRACT_PATH}", file=sys.stderr)
        sys.exit(1)
    return CONTRACT_PATH.read_text(encoding="utf-8")


def compile_contract(source: str, solc_version: str = DEFAULT_SOLC_VERSION):
    install_solc(solc_version)
    compiled = compile_standard(
        {
            "language": "Solidity",
            "sources": {"LiquidationExecutor.sol": {"content": source}},
            "settings": {
                "optimizer": {"enabled": True, "runs": 200},
                "outputSelection": {"*": {"*": ["abi", "evm.bytecode", "evm.deployedBytecode"]}},
            },
        },
        solc_version=solc_version,
    )
    contract_data = compiled["contracts"]["LiquidationExecutor.sol"][CONTRACT_NAME]
    abi = contract_data["abi"]
    bytecode = contract_data["evm"]["bytecode"]["object"]
    return abi, bytecode


_HEADERS = {}


def rpc_call(rpc_url: str, method: str, params):
    body = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
    r = requests.post(rpc_url, json=body, headers=_HEADERS or None, timeout=30)
    r.raise_for_status()
    data = r.json()
    if "error" in data:
        raise RuntimeError(f"RPC error {data['error']}")
    return data["result"]


def hex_strip_0x(s: str) -> str:
    return s[2:] if s.startswith("0x") else s


def to_hex(value: int) -> str:
    return hex(value)


def pad32(hex_no0x: str) -> str:
    s = hex_no0x
    if len(s) > 64:
        return s[-64:]
    return ("0" * (64 - len(s))) + s


def encode_address(addr: str) -> str:
    a = hex_strip_0x(addr)
    if len(a) != 40:
        a = a[-40:].rjust(40, "0")
    return pad32(a)


def build_deploy_data(bytecode_hex: str, pool_address: str) -> str:
    # Constructor(address pool)
    bc = hex_strip_0x(bytecode_hex)
    enc_addr = encode_address(pool_address)
    return "0x" + bc + enc_addr


def wait_receipt(rpc_url: str, tx_hash: str, timeout_sec: int = 300):
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            receipt = rpc_call(rpc_url, "eth_getTransactionReceipt", [tx_hash])
            if receipt is not None:
                return receipt
        except Exception:
            pass
        time.sleep(3)
    raise TimeoutError("Timed out waiting for receipt")


def main():
    # Load .env from repo root if present
    root_env = Path(__file__).resolve().parent.parent / ".env"
    if root_env.exists():
        load_dotenv(root_env)
    parser = argparse.ArgumentParser(description="Deploy LiquidationExecutor (no web3)")
    parser.add_argument("--rpc-url", default=os.getenv("MAINNET_RPC_URL", DEFAULT_MAINNET_RPC), help="RPC URL")
    parser.add_argument("--private-key", default=os.getenv("PRIVATE_KEY", ""), help="Deployer private key (0x...)")
    parser.add_argument("--pool-address", default=os.getenv("MAINNET_AAVE_POOL", MAINNET_AAVE_POOL), help="Aave v3 Pool (proxy) address")
    parser.add_argument("--header", action="append", default=[], help="Extra HTTP header 'Name: Value'. Repeatable.")
    parser.add_argument("--gas", type=int, default=None, help="Override gas limit (units)")
    parser.add_argument("--max-priority-gwei", type=float, default=None, help="Override maxPriorityFeePerGas in gwei")
    parser.add_argument("--max-fee-gwei", type=float, default=None, help="Override maxFeePerGas in gwei")
    args = parser.parse_args()

    if not args.private_key:
        print("Error: Missing --private-key or PRIVATE_KEY env", file=sys.stderr)
        return 1
    # Normalize private key to 0x-prefixed hex
    pk = args.private_key.strip()
    if not pk.startswith("0x") and not pk.startswith("0X"):
        pk = "0x" + pk

    acct: LocalAccount = Account.from_key(pk)
    print(f"Deployer: {acct.address}")

    # Build headers (env override NODIES_API_KEY or provided --header)
    global _HEADERS
    _HEADERS = {}
    api_key = os.getenv("NODIES_API_KEY", "").strip()
    if api_key:
        _HEADERS["x-api-key"] = api_key
    for h in args.header:
        if ":" in h:
            k, v = h.split(":", 1)
            _HEADERS[k.strip()] = v.strip()

    chain_id_hex = rpc_call(args.rpc_url, "eth_chainId", [])
    chain_id = int(chain_id_hex, 16)
    print(f"Chain ID: {chain_id}")

    source = load_contract_source()
    abi, bytecode = compile_contract(source)
    data = build_deploy_data(bytecode, args.pool_address)

    nonce_hex = rpc_call(args.rpc_url, "eth_getTransactionCount", [acct.address, "pending"])
    nonce = int(nonce_hex, 16)

    # Gas params: try EIP-1559 first
    def gwei(v: float) -> int:
        return int(v * 1_000_000_000)

    max_priority = gwei(30.0)  # default 30 gwei
    try:
        prio_hex = rpc_call(args.rpc_url, "eth_maxPriorityFeePerGas", [])
        max_priority = int(prio_hex, 16)
    except Exception:
        pass
    if args.max_priority_gwei is not None:
        max_priority = gwei(args.max_priority_gwei)
    try:
        block = rpc_call(args.rpc_url, "eth_getBlockByNumber", ["latest", False])
        base = int(block.get("baseFeePerGas", "0x0"), 16)
        if args.max_fee_gwei is not None:
            max_fee = gwei(args.max_fee_gwei)
        else:
            max_fee = base * 2 + max_priority
        tx = {
            "from": acct.address,
            "nonce": nonce,
            "chainId": chain_id,
            "to": None,
            "value": 0,
            "data": data,
            "maxPriorityFeePerGas": to_hex(max_priority),
            "maxFeePerGas": to_hex(max_fee),
            "type": "0x2",
        }
    except Exception:
        gas_price_hex = rpc_call(args.rpc_url, "eth_gasPrice", [])
        tx = {
            "from": acct.address,
            "nonce": nonce,
            "chainId": chain_id,
            "to": None,
            "value": 0,
            "data": data,
            "gasPrice": gas_price_hex,
        }

    # Estimate gas
    if args.gas is not None:
        tx["gas"] = hex(args.gas)
    else:
        try:
            gas_hex = rpc_call(args.rpc_url, "eth_estimateGas", [tx])
            gas = int(gas_hex, 16)
            tx["gas"] = hex(int(gas * 1.2))
        except Exception:
            tx["gas"] = hex(1_800_000)

    signed = Account.sign_transaction(tx, private_key=pk)
    tx_hash = rpc_call(args.rpc_url, "eth_sendRawTransaction", [signed.rawTransaction.hex()])
    print(f"Deploy tx: {tx_hash}")
    receipt = wait_receipt(args.rpc_url, tx_hash)
    if int(receipt.get("status", "0x0"), 16) != 1:
        print("Deployment failed (tx reverted)", file=sys.stderr)
        print(json.dumps(receipt, indent=2))
        return 1
    addr = receipt.get("contractAddress")
    print(f"Executor deployed at: {addr}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

