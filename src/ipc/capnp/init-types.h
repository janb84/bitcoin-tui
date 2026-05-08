// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CAPNP_INIT_TYPES_H
#define BITCOIN_TUI_IPC_CAPNP_INIT_TYPES_H

// Pull in the proxy-types headers for every capnp interface returned by
// Init's factory methods so the generated proxy code for Init has access to
// the right CustomBuildField / CustomReadField overloads.
#include <ipc/capnp/mining.capnp.proxy-types.h>
#include <ipc/capnp/rpc.capnp.proxy-types.h>

#include <mp/type-context.h>
#include <mp/type-decay.h>
#include <mp/type-interface.h>
#include <mp/type-threadmap.h>

#endif // BITCOIN_TUI_IPC_CAPNP_INIT_TYPES_H
