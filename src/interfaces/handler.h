// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_HANDLER_H
#define BITCOIN_TUI_INTERFACES_HANDLER_H

namespace interfaces {

//! Generic handler returned by `Chain::handleNotifications`,
//! `Chain::handleRpc`, etc. Mirrors Bitcoin Core's
//! `interfaces::Handler` 1:1 so the capnp schema in
//! `src/ipc/capnp/handler.capnp` is wire-compatible.
//!
//! In bitcoin-tui we never actually invoke handler-returning methods on
//! `Chain` (those are stubbed at the wire level too) — this declaration
//! only exists so that `ProxyClient<Chain>` can be instantiated and
//! `Handler` itself can be used as a return/argument type.
class Handler
{
public:
    virtual ~Handler() = default;
    //! Disconnect the handler.
    virtual void disconnect() = 0;
};

} // namespace interfaces

#endif // BITCOIN_TUI_INTERFACES_HANDLER_H
