// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CLIENT_HPP
#define BITCOIN_TUI_IPC_CLIENT_HPP

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace mp { class EventLoop; }
namespace interfaces { class Init; class Mining; }

// Connects to a bitcoin-node IPC unix socket and exposes the small subset
// of typed interfaces that bitcoin-tui actually uses against a v31.0
// node — currently just Mining, for the poll loop's tip-change wake-up.
//
// Internally the client uses libmultiprocess + Cap'n Proto to talk to the
// node's `interfaces::Mining` capability via Init::makeMining. v31.0 does
// not expose makeRpc, so all JSON-RPC continues to flow over HTTP through
// rpc_client.
class IpcClient
{
public:
    // Connect to a unix socket path. Throws std::runtime_error on failure
    // (socket missing, connection refused, server doesn't speak IPC, etc.).
    explicit IpcClient(const std::string& socket_path);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    // Block until the node's tip moves off `current_tip`, the timeout
    // elapses, or interrupt() is called. Returns the new tip's hex hash
    // on a real change. Note: per v31.0's Mining wire format the server
    // also returns the (unchanged) current tip on timeout — only true
    // shutdown / interrupt yields a null BlockRef, which we surface as
    // std::nullopt. Callers can compare the returned hex to
    // `current_tip_hex` to distinguish "tip changed" from "timed out".
    std::optional<std::string> wait_tip_changed(
        const std::string& current_tip_hex,
        std::chrono::milliseconds timeout);

    // Cancel any in-flight wait_tip_changed() call. Safe to call from
    // another thread.
    void interrupt();

private:
    // Hidden so callers don't need to include any libmultiprocess headers.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // BITCOIN_TUI_IPC_CLIENT_HPP
