// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CLIENT_HPP
#define BITCOIN_TUI_IPC_CLIENT_HPP

#include <memory>
#include <string>

namespace mp { class EventLoop; }
namespace interfaces { class Init; }

// Connects to a bitcoin-node IPC unix socket via libmultiprocess +
// Cap'n Proto and holds the resulting Init capability open. Later
// commits grow this class with the typed accessors bitcoin-tui actually
// calls (Mining.waitTipChanged, etc.).
class IpcClient
{
public:
    // Connect to a unix socket path. Throws std::runtime_error on failure
    // (socket missing, connection refused, path too long, ...).
    explicit IpcClient(const std::string& socket_path);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

private:
    // Hidden so callers don't need to include any libmultiprocess headers.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // BITCOIN_TUI_IPC_CLIENT_HPP
