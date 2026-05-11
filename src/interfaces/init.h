// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_INIT_H
#define BITCOIN_TUI_INTERFACES_INIT_H

namespace interfaces {

//! Bitcoin Core's Init interface, wire-compatible subset used by bitcoin-tui.
//!
//! The capnp schema preserves upstream's interface ID (@0xf2c5cfa319406aa6)
//! so this client can talk to an unmodified v31.0 IPC server. This commit
//! ships only the bare interface; concrete factory methods are added in
//! later commits as bitcoin-tui actually needs them.
class Init
{
public:
    virtual ~Init() = default;
};

} // namespace interfaces

#endif // BITCOIN_TUI_INTERFACES_INIT_H
