// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_INIT_H
#define BITCOIN_TUI_INTERFACES_INIT_H

#include <interfaces/rpc.h>

#include <memory>
#include <stdexcept>

namespace interfaces {

//! Placeholder interface used as the return type for `Init` factory methods
//! that bitcoin-tui does not actually call (`makeEcho`, `makeMining`,
//! `makeChain`). Declaring them in the capnp schema is necessary because
//! Cap'n Proto requires sequential method ordinals matching upstream so the
//! `makeRpc @4` ordinal lands in the right slot.
class Unused
{
public:
    virtual ~Unused() = default;
};

//! Bitcoin Core's Init interface, wire-compatible subset used by bitcoin-tui.
//!
//! The capnp schema preserves upstream's interface ID (@0xf2c5cfa319406aa6)
//! and method ordinals, so this client can talk to an unmodified
//! bitcoin-node IPC server. Only `makeRpc` is wired up; the other slots are
//! defined as stubs returning nullptr because they are never invoked.
class Init
{
public:
    virtual ~Init() = default;
    virtual std::unique_ptr<Unused> makeEcho()    { return nullptr; } // @1
    virtual void makeMiningOld2()                 { throw std::runtime_error("makeMiningOld2 not supported"); } // @2
    virtual std::unique_ptr<Unused> makeMining()  { return nullptr; } // @3
    virtual std::unique_ptr<Rpc>    makeRpc()     { return nullptr; } // @4
    virtual std::unique_ptr<Unused> makeChain()   { return nullptr; } // @5
};

} // namespace interfaces

#endif // BITCOIN_TUI_INTERFACES_INIT_H
