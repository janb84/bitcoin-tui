#include "rpc_client.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t                     = SOCKET;
using io_sz_t                    = int;
static constexpr sock_t kBadSock = INVALID_SOCKET;
static void             net_close(sock_t s) { closesocket(s); }
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t                     = int;
using io_sz_t                    = ssize_t;
static constexpr sock_t kBadSock = -1;
static void             net_close(sock_t s) { close(s); }
#endif

#include <cerrno>
#include <cstring>

RpcClient::RpcClient(RpcConfig config, RpcAuth auth)
    : config_(std::move(config)), auth_(std::move(auth)) {}

// ---------------------------------------------------------------------------
// base64 encoder (RFC 4648)
// ---------------------------------------------------------------------------
std::string RpcClient::base64_encode(const std::string& input) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);

    unsigned char buf[3];
    int           i = 0;

    for (unsigned char c : input) {
        buf[i++] = c;
        if (i == 3) {
            out += chars[(buf[0] >> 2) & 0x3f];
            out += chars[((buf[0] & 0x03) << 4) | ((buf[1] >> 4) & 0x0f)];
            out += chars[((buf[1] & 0x0f) << 2) | ((buf[2] >> 6) & 0x03)];
            out += chars[buf[2] & 0x3f];
            i = 0;
        }
    }
    if (i == 1) {
        out += chars[(buf[0] >> 2) & 0x3f];
        out += chars[(buf[0] & 0x03) << 4];
        out += '=';
        out += '=';
    } else if (i == 2) {
        out += chars[(buf[0] >> 2) & 0x3f];
        out += chars[((buf[0] & 0x03) << 4) | ((buf[1] >> 4) & 0x0f)];
        out += chars[(buf[1] & 0x0f) << 2];
        out += '=';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Low-level HTTP POST over a plain TCP socket
// ---------------------------------------------------------------------------
std::string RpcClient::http_post(const std::string& body) {
#ifdef _WIN32
    static const bool wsa_ok = []() {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    if (!wsa_ok)
        throw RpcError("WSAStartup failed");
#endif

    struct addrinfo  hints = {};
    struct addrinfo* res   = nullptr;

    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_str = std::to_string(config_.port);
    int               err      = getaddrinfo(config_.host.c_str(), port_str.c_str(), &hints, &res);
    if (err != 0)
        throw RpcError("getaddrinfo: " + std::string(gai_strerror(err)));

    sock_t sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == kBadSock) {
        freeaddrinfo(res);
        throw RpcError("socket(): " + std::string(strerror(errno)));
    }

    // Set send/recv timeout
#ifdef _WIN32
    DWORD timeout_ms = static_cast<DWORD>(config_.timeout_seconds) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms),
               sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms),
               sizeof(timeout_ms));
#else
    struct timeval tv = {};
    tv.tv_sec         = config_.timeout_seconds;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        net_close(sock);
        throw RpcError("connect to " + config_.host + ":" + port_str +
                       " failed: " + strerror(errno));
    }
    freeaddrinfo(res);

    // Build HTTP/1.1 request with Connection: close so the server closes after
    // responding, allowing recv() to reach EOF without waiting for a timeout.
    const std::string auth    = base64_encode(auth_.user + ":" + auth_.password);
    const std::string request = "POST / HTTP/1.1\r\n"
                                "Host: " +
                                config_.host +
                                "\r\n"
                                "Authorization: Basic " +
                                auth +
                                "\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: " +
                                std::to_string(body.size()) +
                                "\r\n"
                                "Connection: close\r\n"
                                "\r\n" +
                                body;

    // Send request
    size_t sent_total = 0;
    while (sent_total < request.size()) {
        io_sz_t n = send(sock, request.c_str() + sent_total,
                         static_cast<int>(request.size() - sent_total), 0);
        if (n <= 0) {
            net_close(sock);
            throw RpcError("send() failed: " + std::string(strerror(errno)));
        }
        sent_total += static_cast<size_t>(n);
    }

    // Read response
    std::string response;
    char        buf[4096];
    io_sz_t     n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0)
        response.append(buf, static_cast<size_t>(n));

#ifdef _WIN32
    const int recv_err = (n < 0) ? WSAGetLastError() : 0;
#else
    const int recv_err = (n < 0) ? errno : 0;
#endif
    net_close(sock);

    if (response.empty()) {
#ifdef _WIN32
        if (n < 0 && (recv_err == WSAETIMEDOUT || recv_err == WSAEWOULDBLOCK))
#else
        if (n < 0 && (recv_err == EAGAIN || recv_err == EWOULDBLOCK))
#endif
            throw RpcError("RPC timeout — Bitcoin Core did not respond within " +
                           std::to_string(config_.timeout_seconds) + "s");
        else if (n < 0)
            throw RpcError("recv() failed: " + std::string(strerror(recv_err)));
        else
            throw RpcError(
                "Empty response from Bitcoin Core — connection closed before any data was sent");
    }

    // Extract HTTP status
    auto status_space = response.find(' ');
    if (status_space == std::string::npos)
        throw RpcError("Invalid HTTP response");

    int status_code = 0;
    try {
        status_code = std::stoi(response.substr(status_space + 1, 3));
    } catch (...) {
        throw RpcError("Invalid HTTP status line");
    }

    // Locate body
    auto header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos)
        throw RpcError("No HTTP header separator found");

    std::string resp_body = response.substr(header_end + 4);

    if (status_code == 401)
        throw RpcError("Authentication failed (HTTP 401) — check your RPC credentials");
    // 500 is also used by Bitcoin Core for RPC-level errors; body still contains JSON
    if (status_code != 200 && status_code != 500) {
        std::string detail = resp_body.substr(0, 200);
        if (detail.size() == 200)
            detail += "…";
        throw RpcError("HTTP " + std::to_string(status_code) + ": " + detail);
    }

    return resp_body;
}

// ---------------------------------------------------------------------------
// JSON-RPC call
// ---------------------------------------------------------------------------
json RpcClient::call(const std::string& method, const json& params) {
    // Omit "jsonrpc" version field — Bitcoin Core v25+ rejects "1.1".
    // Legacy JSON-RPC 1.0 (no version field) is accepted by all versions.
    json req = {
        {"id", ++request_id_},
        {"method", method},
        {"params", params},
    };

    const std::string body     = req.dump();
    const std::string response = http_post(body);

    json parsedJson;
    try {
        parsedJson = json::parse(response);
    } catch (const json::exception& e) {
        throw RpcError("JSON parse error: " + std::string(e.what()));
    }

    if (parsedJson.contains("error") && !parsedJson["error"].is_null()) {
        const json& err = parsedJson["error"];
        std::string msg;

        if (err.contains("message") && err["message"].is_string()) {
            msg = err["message"].get<std::string>();
        } else {
            msg = "RPC error";
            if (err.contains("code") &&
                (err["code"].is_number_integer() || err["code"].is_number())) {
                msg += " (code " + std::to_string(err["code"].get<long long>()) + ")";
            }
            msg += ": " + err.dump();
        }
        throw RpcError(msg);
    }
    return parsedJson;
}
