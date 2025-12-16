#include "net/ws_client.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#if defined(_WIN32)
#pragma comment(lib, "winhttp.lib")
#include <winhttp.h>
#endif

static std::wstring ToWide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static std::string NormalizeWsUrl(const std::string& url) {
    if (url.rfind("wss://", 0) == 0) return std::string("https://") + url.substr(6);
    if (url.rfind("ws://", 0) == 0) return std::string("http://") + url.substr(5);
    return url;
}

WsClient::WsClient() : open_(false) {}
WsClient::~WsClient() { Close(); }

bool WsClient::Connect(const std::string& url,
                       const std::unordered_map<std::string, std::string>& headers) {
#if defined(_WIN32)
    Close();
    
    // Set reasonable timeouts to prevent hanging
    DWORD timeout = 10000; // 10 seconds
    
    h_session_ = WinHttpOpen(L"DefiLiquidationBot/1.0",
                             WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h_session_) return false;
    
    // Set session timeouts
    WinHttpSetTimeouts(h_session_, timeout, timeout, timeout, timeout);

    URL_COMPONENTS uc{};
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    std::string norm = NormalizeWsUrl(url);
    std::wstring wurl = ToWide(norm);
    std::wstring host(256, L'\0');
    std::wstring path(2048, L'\0');
    std::wstring extra(1024, L'\0');
    uc.lpszHostName = host.data(); uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data(); uc.dwUrlPathLength = (DWORD)path.size();
    uc.lpszExtraInfo = extra.data(); uc.dwExtraInfoLength = (DWORD)extra.size();
    
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) { 
        Close(); 
        return false; 
    }
    
    bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    if (uc.nPort == 0) uc.nPort = secure ? 443 : 80;
    host.resize(uc.dwHostNameLength);
    path.resize(uc.dwUrlPathLength);
    extra.resize(uc.dwExtraInfoLength);
    std::wstring object = path + extra;

    h_connect_ = WinHttpConnect(h_session_, host.c_str(), uc.nPort, 0);
    if (!h_connect_) { 
        Close(); 
        return false; 
    }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    h_request_ = WinHttpOpenRequest(h_connect_, L"GET", object.c_str(), NULL,
                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    flags);
    if (!h_request_) { 
        Close(); 
        return false; 
    }

    // Set request timeouts
    WinHttpSetTimeouts(h_request_, timeout, timeout, timeout, timeout);

    if (!WinHttpSetOption(h_request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) { 
        Close(); 
        return false; 
    }

    // Build proper WebSocket headers following QuickNode standards
    std::wstring add_headers;
    
    // Add custom headers from user
    for (const auto& kv : headers) {
        add_headers += ToWide(kv.first);
        add_headers += L": ";
        add_headers += ToWide(kv.second);
        add_headers += L"\r\n";
    }
    
    // Add standard WebSocket headers
    add_headers += L"User-Agent: DefiLiquidationBot/1.0\r\n";
    add_headers += L"Origin: https://defiliquidation.com\r\n";
    add_headers += L"Sec-WebSocket-Protocol: json-rpc\r\n";
    
    LPCWSTR add = add_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : add_headers.c_str();
    DWORD add_len = add_headers.empty() ? 0 : (DWORD)add_headers.size();

    if (!WinHttpSendRequest(h_request_, add, add_len, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) { 
        Close(); 
        return false; 
    }
    
    if (!WinHttpReceiveResponse(h_request_, NULL)) { 
        Close(); 
        return false; 
    }

    h_ws_ = WinHttpWebSocketCompleteUpgrade(h_request_, NULL);
    WinHttpCloseHandle(h_request_); h_request_ = nullptr;
    if (!h_ws_) { 
        Close(); 
        return false; 
    }
    
    open_ = true;
    return true;
#else
    (void)url; (void)headers; open_ = false; return false;
#endif
}

bool WsClient::SendText(const std::string& message) {
#if defined(_WIN32)
    if (!open_ || !h_ws_) return false;
    DWORD res = WinHttpWebSocketSend(h_ws_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                     (PBYTE)message.data(), (DWORD)message.size());
    return res == NO_ERROR;
#else
    (void)message; return false;
#endif
}

bool WsClient::SendPing() {
#if defined(_WIN32)
    if (!open_ || !h_ws_) return false;
    // Send a minimal JSON-RPC ping to keep connection alive
    // This is more compatible than raw WebSocket ping frames
    const std::string ping_msg = "{\"jsonrpc\":\"2.0\",\"id\":999,\"method\":\"eth_blockNumber\",\"params\":[]}";
    DWORD res = WinHttpWebSocketSend(h_ws_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                     (PBYTE)ping_msg.data(), (DWORD)ping_msg.size());
    return res == NO_ERROR;
#else
    return false;
#endif
}

bool WsClient::RecvText(std::string& out_message, int /*timeout_ms*/) {
#if defined(_WIN32)
    if (!open_ || !h_ws_) return false;
    std::vector<char> buffer(16 * 1024);
    out_message.clear();
    for (;;) {
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        DWORD bytes_read = 0;
        DWORD res = WinHttpWebSocketReceive(h_ws_, (PBYTE)buffer.data(), (DWORD)buffer.size(), &bytes_read, &type);
        if (res != NO_ERROR) return false;
        
        // Handle different message types
        switch (type) {
            case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
                open_ = false; 
                return false;
                
            case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
            case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
                if (bytes_read > 0) {
                    out_message.append(buffer.data(), buffer.data() + bytes_read);
                }
                if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                    return true; // Complete message received
                }
                // Continue for fragments
                break;
                
            case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
                // Binary message, skip for now
                return true;
                
            default:
                continue;
        }
    }
#else
    (void)out_message; return false;
#endif
}

void WsClient::Close() {
#if defined(_WIN32)
    if (h_ws_) { 
        WinHttpWebSocketClose(h_ws_, 1000, NULL, 0); 
        WinHttpCloseHandle(h_ws_); 
        h_ws_ = nullptr; 
    }
    if (h_request_) { WinHttpCloseHandle(h_request_); h_request_ = nullptr; }
    if (h_connect_) { WinHttpCloseHandle(h_connect_); h_connect_ = nullptr; }
    if (h_session_) { WinHttpCloseHandle(h_session_); h_session_ = nullptr; }
#endif
    open_ = false;
}

bool WsClient::IsOpen() const { return open_; }


