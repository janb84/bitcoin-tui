// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CAPNP_COMMON_TYPES_H
#define BITCOIN_TUI_IPC_CAPNP_COMMON_TYPES_H

// Custom field adapters needed by libmultiprocess for the types referenced in
// common.capnp ($Proxy.wrap targets and Data fields). interfaces/types.h
// already provides the C++ definitions of `interfaces::BlockRef` and
// `interfaces::uint256`; we just need to teach mp how to read/write `uint256`
// over the Cap'n Proto `Data` wire type.

#include <interfaces/types.h>

#include <mp/proxy-types.h>
#include <mp/type-data.h>
#include <mp/type-decay.h>
#include <mp/type-struct.h>

#endif // BITCOIN_TUI_IPC_CAPNP_COMMON_TYPES_H
