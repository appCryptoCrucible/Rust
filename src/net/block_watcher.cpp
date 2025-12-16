#include "net/block_watcher.hpp"
#include "node_connection/rpc_client.hpp"
#include "utils/json_rpc.hpp"
#include "common/logger.hpp"
#include "net/ws_client.hpp"
#include "common/config_manager.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>
#include <random>

// Forward declare before use
static unsigned long long ParseHexToULL(const std::string& hex);

void BlockWatcher::RunWsLoop() {
    Logger::Info("=== Starting WebSocket block detection ===");
    // Primary and backup WS endpoints
    std::vector<std::string> endpoints;
    if (auto p = ConfigManager::Get("WEBSOCKET_RPC_URL"); p && !p->empty()) endpoints.emplace_back(*p);
    if (auto b = ConfigManager::Get("WEBSOCKET_RPC_URL_BACKUP"); b && !b->empty()) endpoints.emplace_back(*b);
    if (endpoints.empty()) {
        Logger::Info("No WebSocket URLs found in environment");
        throw std::runtime_error("WS URL not set");
    }
    Logger::Info("Found " + std::to_string(endpoints.size()) + " WebSocket endpoint(s)");
    for (const auto& url : endpoints) {
        Logger::Info("  - " + url);
    }

    // Optional auth header (generic, e.g., QuickNode: x-api-key: <key>)
    std::unordered_map<std::string, std::string> headers;
    if (auto auth = ConfigManager::Get("WS_AUTH_HEADER"); auth && !auth->empty()) {
        std::string hdr = *auth;
        auto pos = hdr.find(':');
        if (pos != std::string::npos) {
            std::string name = hdr.substr(0, pos);
            std::string value = hdr.substr(pos + 1);
            auto ltrim = [](std::string& s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); })); };
            auto rtrim = [](std::string& s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end()); };
            ltrim(name); rtrim(name); ltrim(value); rtrim(value);
            if (!name.empty() && !value.empty()) headers[name] = value;
        }
    }

    // MAIN PERSISTENT CONNECTION LOOP - Only reconnect on actual failure
    while (running_.load(std::memory_order_relaxed)) {
        // Try to establish a persistent connection to any endpoint
        WsClient* persistent_ws = nullptr;
        std::string connected_url;
        
        for (const auto& url : endpoints) {
            if (!running_.load(std::memory_order_relaxed)) {
                throw std::runtime_error("WS stopped");
            }

            Logger::Info("Attempting to establish persistent connection to: " + url);
            persistent_ws = new WsClient();
            
            if (!persistent_ws->Connect(url, headers)) {
                Logger::Warning(std::string("WS connect failed for ") + url);
                std::cout << "[!] WebSocket connection failed for: " << url << std::endl;
                delete persistent_ws;
                persistent_ws = nullptr;
                continue; // Try next endpoint
            }

            std::cout << "\n[+] WebSocket CONNECTED: " << url << std::endl;
            Logger::Info(std::string("WS connected: ") + url);
            connected_url = url;
            break; // Successfully connected, exit endpoint loop
        }
        
        if (!persistent_ws) {
            Logger::Error("Failed to connect to any endpoint, waiting 10 seconds before retry");
            std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            continue;
        }

        // Subscribe to newHeads ONCE - NEVER resubscribe
        const std::string sub = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_subscribe\",\"params\":[\"newHeads\"]}";
        Logger::Info("Sending subscription request...");
        if (!persistent_ws->SendText(sub)) { 
            Logger::Warning("Failed to send subscription request");
            persistent_ws->Close();
            delete persistent_ws;
            continue; // Try to reconnect
        }
        Logger::Info("Subscription sent successfully");

        // Wait for subscription confirmation
        std::string confirm_msg;
        bool subscription_confirmed = false;
        auto subscription_start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - subscription_start).count() < 5000) {
            if (persistent_ws->RecvText(confirm_msg, 100)) {
                if (confirm_msg.find("\"id\":1") != std::string::npos && 
                    confirm_msg.find("\"result\"") != std::string::npos) {
                    subscription_confirmed = true;
                    Logger::Info("Subscription confirmed: " + confirm_msg);
                    break;
                }
            }
        }
        
        if (!subscription_confirmed) {
            Logger::Warning("Subscription confirmation timeout");
            persistent_ws->Close();
            delete persistent_ws;
            continue; // Try to reconnect
        }

        Logger::Info("=== WebSocket subscription active - maintaining persistent connection ===");
        Logger::Info("This connection should last for hours/days - no more subscriptions!");
        
        // MAIN PERSISTENT CONNECTION LOOP - Only exit on actual connection failure
        unsigned long long last_block = 0ULL;
        auto last_ping = std::chrono::steady_clock::now();
        const int ping_interval_ms = 300000; // 5 minutes - much less frequent
        int ping_response_count = 0;
        auto last_block_time = std::chrono::steady_clock::now();
        auto connection_start = std::chrono::steady_clock::now();
        bool connection_failed = false;
        
        Logger::Info("=== ENTERING MAIN CONNECTION LOOP ===");
        Logger::Info("Connection should persist until actual failure");

        while (running_.load(std::memory_order_relaxed) && persistent_ws->IsOpen() && !connection_failed) {
            auto now = std::chrono::steady_clock::now();
            
            // Send WebSocket ping frame every 5 minutes (much less frequent)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ping).count() >= ping_interval_ms) {
                last_ping = now;
                if (!persistent_ws->SendPing()) {
                    Logger::Warning("Failed to send WebSocket ping");
                    // Don't immediately disconnect - just log the issue
                } else {
                    Logger::Info("Sent WebSocket ping (id:999)");
                }
            }

            // Check connection health every 2 minutes (less aggressive)
            static auto last_health_check = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_health_check).count() >= 120000) {
                last_health_check = now;
                
                // Only check if connection is actually closed
                if (!persistent_ws->IsOpen()) {
                    Logger::Warning("WebSocket connection actually closed");
                    connection_failed = true;
                    break;
                }
                
                // Check if we're receiving blocks regularly - very tolerant
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_block_time).count() > 600) {
                    Logger::Warning("No blocks received for 10+ minutes, connection may be stale");
                    connection_failed = true;
                    break;
                }
                
                // Log connection uptime every 10 minutes
                auto uptime = std::chrono::duration_cast<std::chrono::minutes>(now - connection_start).count();
                if (uptime > 0 && uptime % 10 == 0) {
                    Logger::Info("WebSocket connection stable for " + std::to_string(uptime) + " minutes");
                }
            }

            std::string msg;
            if (!persistent_ws->RecvText(msg, 100)) { // Increased timeout for ping/pong handling
                continue;
            }
            
            // Handle ping response (eth_blockNumber response with id:999)
            if (msg.find("\"id\":999") != std::string::npos) {
                ping_response_count++;
                Logger::Info("Received ping response #" + std::to_string(ping_response_count) + " (id:999)");
                continue;
            }
            
            // Handle subscription notification for new block number
            auto pos = msg.find("\"number\":\"");
            if (pos != std::string::npos) {
                pos += 10;
                auto end = msg.find('"', pos);
                if (end != std::string::npos) {
                    auto hex = msg.substr(pos, end - pos);
                    unsigned long long bn = ParseHexToULL(hex);
                    if (bn > last_block) {
                        last_block = bn;
                        last_block_time = now; // Update last block time
                        if (on_block_) on_block_(bn);
                    }
                }
            }
        }
        
        // Connection failed - clean up and prepare to reconnect
        Logger::Warning("=== CONNECTION LOOP EXITED ===");
        Logger::Warning("Reason: running_=" + std::to_string(running_.load(std::memory_order_relaxed)) + 
                       ", IsOpen=" + std::to_string(persistent_ws->IsOpen()) + 
                       ", connection_failed=" + std::to_string(connection_failed));
        
        if (!running_.load(std::memory_order_relaxed)) {
            Logger::Info("Bot stopped, exiting WebSocket loop");
            persistent_ws->Close();
            delete persistent_ws;
            return; // Exit completely
        }
        
        Logger::Warning("WebSocket connection failed, will reconnect");
        persistent_ws->Close();
        delete persistent_ws;
        persistent_ws = nullptr;
        
        std::cout << "\n[!] WEBSOCKET DISCONNECTED: " << connected_url << std::endl;
        std::cout << "[!] Will attempt to reconnect..." << std::endl;
        Logger::Warning(std::string("WS disconnected: ") + connected_url);
        
        // Brief delay before trying to reconnect
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

static unsigned long long ParseHexToULL(const std::string& hex) {
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        return std::stoull(hex.substr(2), nullptr, 16);
    }
    return std::stoull(hex, nullptr, 16);
}

void BlockWatcher::RunFilterLoop() {
    unsigned long long last_block = 0ULL;
    try {
        filter_id_ = rpc_.EthNewBlockFilter(500);
    } catch (...) {
        // Fallback to polling loop
        Run();
        return;
    }
    int sleep_ms = 10;
    while (running_.load(std::memory_order_relaxed)) {
        try {
            const std::string changes_json = rpc_.EthGetFilterChanges(filter_id_, 500);
            // Expected: ["0xblockhash", ...]
            if (changes_json.size() >= 2 && changes_json[0] == '[') {
                // Each new block hash implies at least one new block advanced; fetch number for accuracy
                auto num_hex = rpc_.EthBlockNumber(400);
                unsigned long long bn = num_hex.empty() ? 0ULL : ParseHexToULL(num_hex);
                if (bn > last_block) {
                    last_block = bn;
                    if (on_block_) on_block_(bn);
                }
                sleep_ms = 10; // stay hot
            } else {
                // No changes
                sleep_ms = 20;
            }
        } catch (...) {
            sleep_ms = 40;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    // Cleanup
    try { if (!filter_id_.empty()) rpc_.EthUninstallFilter(filter_id_, 300); } catch (...) {}
}

void BlockWatcher::Run() {
    // Try WebSocket with smart batching first, fall back to HTTP filter if needed
    try {
        std::cout << "[+] Using WebSocket with smart block batching" << std::endl;
        Logger::Info("Using WebSocket with smart block batching");
        RunWsLoop();
        Logger::Info("WebSocket loop completed successfully");
        return;
    } catch (...) {
        std::cout << "\n[!] WebSocket failed, falling back to HTTP filter..." << std::endl;
        Logger::Warning("WebSocket failed, falling back to HTTP filter");
        // fall through
    }
    
    // Try HTTP filter next for near-WS latency with lower overhead than raw polling
    try {
        std::cout << "[+] Using HTTP eth_newBlockFilter fallback" << std::endl;
        RunFilterLoop();
        Logger::Info("Using HTTP eth_newBlockFilter fallback");
        return;
    } catch (...) {
        std::cout << "[!] HTTP filter failed, falling back to polling..." << std::endl;
        Logger::Warning("HTTP filter failed, falling back to polling");
        // fall through to simple polling
    }
    
    std::cout << "[+] Using simple polling fallback" << std::endl;
    Logger::Info("Using simple polling fallback");
    unsigned long long last_block = 0ULL;
    int backoff_ms = 10; // aggressively low for near-WS latency
    const int backoff_max_ms = 80;
    while (running_.load(std::memory_order_relaxed)) {
        try {
            // lighter method for just block number
            auto num_hex = rpc_.EthBlockNumber(600);
            unsigned long long bn = 0ULL;
            if (!num_hex.empty()) {
                // EthBlockNumber returns hex string like 0x...; ensure it's hex
                bn = ParseHexToULL(num_hex);
            }
            if (bn > last_block) {
                last_block = bn;
                backoff_ms = 10; // maintain low latency on success
                if (on_block_) on_block_(bn);
            }
        } catch (const std::exception& ex) {
            Logger::Warning(std::string("BlockWatcher error: ") + ex.what());
            backoff_ms = (backoff_ms * 2 < backoff_max_ms) ? backoff_ms * 2 : backoff_max_ms;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
    }
}




