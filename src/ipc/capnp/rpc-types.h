// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CAPNP_RPC_TYPES_H
#define BITCOIN_TUI_IPC_CAPNP_RPC_TYPES_H

// Pulls in the libmultiprocess type adapters needed by the generated proxy
// code for `Rpc.executeRpc(Text, Text, Text) -> Text` (string args, returns
// a string, takes a Proxy.Context).
#include <mp/type-context.h>
#include <mp/type-decay.h>
#include <mp/type-string.h>

#endif // BITCOIN_TUI_IPC_CAPNP_RPC_TYPES_H
