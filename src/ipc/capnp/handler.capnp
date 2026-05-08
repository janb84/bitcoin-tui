# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Wire-compatible 1:1 copy of Bitcoin Core's src/ipc/capnp/handler.capnp.
# `Handler` is referenced as a return type by Chain.handleNotifications and
# Chain.handleRpc — bitcoin-tui never calls those methods, so this interface
# is effectively just a placeholder needed for ordinal layout.

@0xebd8f46e2f369076;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("ipc::capnp::messages");

using Proxy = import "/mp/proxy.capnp";
$Proxy.include("interfaces/handler.h");
$Proxy.includeTypes("ipc/capnp/handler-types.h");

interface Handler $Proxy.wrap("interfaces::Handler") {
    destroy    @0 (context :Proxy.Context) -> ();
    disconnect @1 (context :Proxy.Context) -> ();
}
