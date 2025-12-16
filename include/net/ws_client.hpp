#pragma once
#include <string>
#include <unordered_map>
#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

// Minimal WebSocket client wrapper. Uses libcurl WS API when available;
// otherwise, methods will return false and callers should fallback.
class WsClient {
public:
    WsClient();
    ~WsClient();

    bool Connect(const std::string& url,
                 const std::unordered_map<std::string, std::string>& headers = {});
    bool SendText(const std::string& message);
    bool SendPing(); // Send WebSocket ping frame
    // Returns true on success; false on timeout or error. timeout_ms <= 0 blocks briefly.
    bool RecvText(std::string& out_message, int timeout_ms);
    void Close();
    bool IsOpen() const;

private:
    bool open_;
#if defined(_WIN32)
    HINTERNET h_session_ = nullptr;
    HINTERNET h_connect_ = nullptr;
    HINTERNET h_request_ = nullptr;
    HINTERNET h_ws_ = nullptr;
#endif
};


