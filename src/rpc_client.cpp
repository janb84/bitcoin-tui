#include "rpc_client.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

RpcClient::RpcClient(RpcConfig config) : config_(std::move(config)) {}

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
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_str = std::to_string(config_.port);
    int               err      = getaddrinfo(config_.host.c_str(), port_str.c_str(), &hints, &res);
    if (err != 0) {
        throw RpcError("getaddrinfo: " + std::string(gai_strerror(err)));
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        throw RpcError("socket(): " + std::string(strerror(errno)));
    }

    // Set send/recv timeout
    struct timeval tv{};
    tv.tv_sec = config_.timeout_seconds;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(sock);
        throw RpcError("connect to " + config_.host + ":" + port_str +
                       " failed: " + strerror(errno));
    }
    freeaddrinfo(res);

    // Build HTTP/1.0 request (avoids chunked transfer encoding)
    const std::string auth    = base64_encode(config_.user + ":" + config_.password);
    const std::string request = "POST / HTTP/1.0\r\n"
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
                                "\r\n" +
                                body;

    // Send request
    size_t sent_total = 0;
    while (sent_total < request.size()) {
        ssize_t n = send(sock, request.c_str() + sent_total, request.size() - sent_total, 0);
        if (n <= 0) {
            close(sock);
            throw RpcError("send() failed: " + std::string(strerror(errno)));
        }
        sent_total += n;
    }

    // Read response
    std::string response;
    char        buf[4096];
    ssize_t     n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, static_cast<size_t>(n));
    }
    close(sock);

    if (response.empty()) {
        throw RpcError("Empty response from Bitcoin Core");
    }

    // Extract HTTP status
    auto status_space = response.find(' ');
    if (status_space == std::string::npos) {
        throw RpcError("Invalid HTTP response");
    }
    int status_code = 0;
    try {
        status_code = std::stoi(response.substr(status_space + 1, 3));
    } catch (...) {
        throw RpcError("Invalid HTTP status line");
    }

    // Locate body
    auto header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw RpcError("No HTTP header separator found");
    }
    std::string resp_body = response.substr(header_end + 4);

    if (status_code == 401) {
        throw RpcError("Authentication failed — check your RPC credentials");
    }
    // 500 is also used by Bitcoin Core for RPC-level errors; body still contains JSON
    if (status_code != 200 && status_code != 500) {
        throw RpcError("HTTP " + std::to_string(status_code));
    }

    return resp_body;
}

// ---------------------------------------------------------------------------
// JSON-RPC call
// ---------------------------------------------------------------------------
json RpcClient::call(const std::string& method, const json& params) {
    json req = {
        {"jsonrpc", "1.1"},
        {"id", ++request_id_},
        {"method", method},
        {"params", params},
    };

    const std::string body     = req.dump();
    const std::string response = http_post(body);

    json j;
    try {
        j = json::parse(response);
    } catch (const json::exception& e) {
        throw RpcError("JSON parse error: " + std::string(e.what()));
    }

    if (j.contains("error") && !j["error"].is_null()) {
        std::string msg = "RPC error";
        if (j["error"].contains("message")) {
            msg = j["error"]["message"].get<std::string>();
        }
        throw RpcError(msg);
    }

    return j;
}
