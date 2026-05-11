// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CAPNP_MINING_TYPES_H
#define BITCOIN_TUI_IPC_CAPNP_MINING_TYPES_H

// Custom field adapters needed by libmultiprocess for the types referenced in
// mining.capnp ($Proxy.wrap targets and Data fields).

#include <interfaces/mining.h>
#include <interfaces/types.h>
#include <ipc/capnp/common.capnp.proxy-types.h>

#include <mp/proxy-types.h>
#include <mp/type-chrono.h>
#include <mp/type-context.h>
#include <mp/type-data.h>
#include <mp/type-decay.h>
#include <mp/type-number.h>
#include <mp/type-optional.h>
#include <mp/type-struct.h>

#endif // BITCOIN_TUI_IPC_CAPNP_MINING_TYPES_H
