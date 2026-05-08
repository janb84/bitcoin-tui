// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CAPNP_MINING_TYPES_H
#define BITCOIN_TUI_IPC_CAPNP_MINING_TYPES_H

// Type adapters required by the generated proxy code for mining.capnp:
// - $Proxy.wrap("interfaces::BlockRef") struct        → mp/type-struct.h
// - Data fields (uint256)                          → mp/type-data.h
// - Bool / Int32 / Float64 fields                  → mp/type-number.h
// - chrono duration parameters                     → mp/type-chrono.h
// - std::optional<BlockRef> return type            → mp/type-optional.h
// - context :Proxy.Context                         → mp/type-context.h
//
// proxy-types.h must come before type-optional.h: the latter uses
// ReadDestUpdate which is defined in proxy-types.h.
#include <mp/proxy-types.h>

#include <mp/type-context.h>
#include <mp/type-chrono.h>
#include <mp/type-data.h>
#include <mp/type-decay.h>
#include <mp/type-interface.h>
#include <mp/type-number.h>
#include <mp/type-optional.h>
#include <mp/type-struct.h>

#endif // BITCOIN_TUI_IPC_CAPNP_MINING_TYPES_H
