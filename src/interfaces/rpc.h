// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_RPC_H
#define BITCOIN_TUI_INTERFACES_RPC_H

#include <memory>
#include <string>

namespace interfaces {

//! Bitcoin Core's Rpc interface, wire-compatible subset used by bitcoin-tui.
//!
//! The capnp wire-level signature is `executeRpc(Text, Text, Text) -> Text`;
//! Bitcoin Core wraps Text as UniValue on its side, but bitcoin-tui has its
//! own JSON layer (nlohmann::json) so we keep the C++ signature as plain
//! std::string and let callers parse the response themselves.
class Rpc
{
public:
    virtual ~Rpc() = default;
    //! Execute a single JSON-RPC request and return the JSON response body.
    //! @param request  JSON-encoded request, e.g. {"method":"getblockchaininfo","params":[]}
    //! @param uri      HTTP URI ("/" for the node, "/wallet/<name>" for wallets)
    //! @param user     Username for the request (may be empty)
    virtual std::string executeRpc(std::string request, std::string uri, std::string user) = 0;
};

} // namespace interfaces

#endif // BITCOIN_TUI_INTERFACES_RPC_H
