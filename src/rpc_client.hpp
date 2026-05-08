#pragma once

#include "json.hpp"
#include <memory>
#include <stdexcept>
#include <string>

class IpcClient;

class RpcError : public std::runtime_error {
  public:
    explicit RpcError(const std::string& msg) : std::runtime_error(msg) {}
};

// Kept at namespace scope to avoid a clang bug where nested structs with
// default member initializers trigger "needed within definition of enclosing
// class outside of member functions".
struct RpcConfig {
    std::string host            = "127.0.0.1";
    int         port            = 8332;
    int         timeout_seconds = 30;
};

struct RpcAuth {
    std::string user;
    std::string password;
};

class RpcClient {
  public:
    explicit RpcClient(RpcConfig config, RpcAuth auth);
    ~RpcClient();
    RpcClient(RpcClient&&) noexcept;
    RpcClient& operator=(RpcClient&&) noexcept;

    // Try to switch this client over to a libmultiprocess IPC connection at
    // `socket_path`. Returns true on success; on failure leaves the client in
    // its previous state (HTTP) and returns false. After a successful call
    // all `call()` / `call_wallet()` invocations are routed over IPC instead
    // of HTTP.
    bool try_use_ipc(const std::string& socket_path);

    // True if calls are currently being routed over IPC.
    bool using_ipc() const noexcept;

    json call(const std::string& method, const json& params = json::array());
    json call_wallet(const std::string& wallet, const std::string& method,
                     const json& params = json::array());

  private:
    json      call(const std::string& endpoint, const std::string& method, const json& params);
    RpcConfig config_;
    RpcAuth   auth_;
    int       request_id_ = 0;
    std::unique_ptr<IpcClient> ipc_;

    std::string        http_post(const std::string& endpoint, const std::string& body);
    static std::string base64_encode(const std::string& input);
};
