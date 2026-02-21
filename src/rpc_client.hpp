#pragma once

#include "json.hpp"
#include <stdexcept>
#include <string>

class RpcError : public std::runtime_error {
public:
    explicit RpcError(const std::string& msg) : std::runtime_error(msg) {}
};

// Kept at namespace scope to avoid a clang bug where nested structs with
// default member initializers trigger "needed within definition of enclosing
// class outside of member functions".
struct RpcConfig {
    std::string host           = "127.0.0.1";
    int         port           = 8332;
    std::string user;
    std::string password;
    int         timeout_seconds = 10;
};

class RpcClient {
public:
    explicit RpcClient(RpcConfig config = {});

    json call(const std::string& method,
                        const json& params = json::array());

private:
    RpcConfig   config_;
    int         request_id_ = 0;

    std::string http_post(const std::string& body);
    static std::string base64_encode(const std::string& input);
};
