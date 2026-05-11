# Copyright (c) 2021-present The Bitcoin Core developers
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Wire-compatible subset of Bitcoin Core's src/ipc/capnp/init.capnp as
# shipped in v31.0. Preserves the interface ID (@0xf2c5cfa319406aa6) and
# the @1/@2/@3 ordinals so this client can talk to an unmodified v31.0
# bitcoin-node IPC server. `makeEcho @1` is declared as a placeholder
# `Unused` capability so we don't need to import echo.capnp; only
# `makeMining @3` is actually used.

@0xf2c5cfa319406aa6;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("ipc::capnp::messages");

using Proxy = import "/mp/proxy.capnp";
$Proxy.include("interfaces/init.h");
$Proxy.includeTypes("ipc/capnp/init-types.h");

using Mining = import "mining.capnp";

interface Unused $Proxy.wrap("interfaces::Unused") {
    destroy @0 (context :Proxy.Context) -> ();
}

interface Init $Proxy.wrap("interfaces::Init") {
    construct      @0 (threadMap: Proxy.ThreadMap) -> (threadMap :Proxy.ThreadMap);
    makeEcho       @1 (context :Proxy.Context) -> (result :Unused);
    makeMiningOld2 @2 () -> ();
    makeMining     @3 (context :Proxy.Context) -> (result :Mining.Mining);
}
