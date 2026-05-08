// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_MINING_H
#define BITCOIN_TUI_INTERFACES_MINING_H

#include "types.h"

#include <optional>
#include <stdexcept>

namespace interfaces {

// Local stand-in for Bitcoin Core's interfaces::Mining. Only the methods we
// actually call are real; createNewBlock/checkBlock are placeholder stubs so
// the proxy class can be instantiated, but they take/return primitive types
// instead of the upstream option / template structs to avoid pulling in
// node/types.h, validation.h, consensus types, etc.
//
// The capnp schema in src/ipc/capnp/mining.capnp must match this declaration
// method-by-method (ordinals 0..6) for libmultiprocess's generated
// ProxyClient<Mining> to compile.
class Mining
{
public:
    virtual ~Mining() = default;

    // @0
    virtual bool isTestChain() = 0;
    // @1
    virtual bool isInitialBlockDownload() = 0;
    // @2
    virtual std::optional<BlockRef> getTip() = 0;
    // @3 — blocks until tip != current_tip or timeout/interrupt fires.
    virtual std::optional<BlockRef> waitTipChanged(
        uint256 current_tip,
        MillisecondsDouble timeout = MillisecondsDouble::max()) = 0;

    // Ordinals @4 and @5 are placeholder stubs (createNewBlock / checkBlock
    // upstream). bitcoin-tui never invokes them; they're here only so the
    // class layout matches the capnp schema. The signatures are deliberately
    // simplified to avoid needing node/miner.h-style types.
    virtual bool createNewBlock(bool /*unused*/)            { throw std::runtime_error("createNewBlock not supported in bitcoin-tui IPC stub"); }
    virtual bool checkBlock(bool /*unused*/)                { throw std::runtime_error("checkBlock not supported in bitcoin-tui IPC stub"); }

    // @6
    virtual void interrupt() = 0;
};

} // namespace interfaces

#endif // BITCOIN_TUI_INTERFACES_MINING_H
