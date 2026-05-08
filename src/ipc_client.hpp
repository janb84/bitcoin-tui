// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CLIENT_HPP
#define BITCOIN_TUI_IPC_CLIENT_HPP

#include "json.hpp"

#include <memory>
#include <string>

namespace mp { class EventLoop; }
namespace interfaces { class Init; class Rpc; }

// Connects to a bitcoin-node IPC unix socket and exposes a JSON-RPC API
// matching the subset of `RpcClient` used by bitcoin-tui.
//
// Internally the client uses libmultiprocess + Cap'n Proto to talk to the
// node's `interfaces::Rpc::executeRpc` capability. The wire format is a JSON
// string in / JSON string out, so callers see exactly the same JSON they
// would get from the HTTP RPC interface.
class IpcClient
{
public:
    // Connect to a unix socket path. Throws std::runtime_error on failure.
    explicit IpcClient(const std::string& socket_path);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    // JSON-RPC call against "/" endpoint.
    json call(const std::string& method, const json& params);

    // JSON-RPC call against "/wallet/<wallet>" endpoint.
    json call_wallet(const std::string& wallet,
                     const std::string& method,
                     const json& params);

private:
    // Hidden so callers don't need to include any libmultiprocess headers.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // BITCOIN_TUI_IPC_CLIENT_HPP
