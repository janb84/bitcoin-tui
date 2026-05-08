// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_IPC_CAPNP_CHAIN_TYPES_H
#define BITCOIN_TUI_IPC_CAPNP_CHAIN_TYPES_H

// libmultiprocess type adapters for the C++ types referenced by chain.capnp
// (see src/interfaces/chain.h). Stubbed methods fall back to mp::Bool, so
// only the adapters needed by the *real* methods are pulled in here.
//
// proxy-types.h must come first: type-optional.h and type-vector.h depend on
// the ReadDestUpdate machinery defined there.
#include <mp/proxy-types.h>

#include <mp/type-chrono.h>
#include <mp/type-context.h>
#include <mp/type-data.h>
#include <mp/type-decay.h>
#include <mp/type-interface.h>
#include <mp/type-number.h>
#include <mp/type-optional.h>
#include <mp/type-pointer.h>
#include <mp/type-string.h>
#include <mp/type-struct.h>
#include <mp/type-vector.h>

#include <ipc/capnp/handler.capnp.proxy-types.h>

#endif // BITCOIN_TUI_IPC_CAPNP_CHAIN_TYPES_H
