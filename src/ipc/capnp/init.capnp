# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# IDs and ordinals match Bitcoin Core's src/ipc/capnp/init.capnp so this
# client is wire-compatible with an unmodified bitcoin-node IPC server.
# Only `makeRpc @4` is actually called by bitcoin-tui; the other ordinals
# return placeholder `Unused` capabilities so that Cap'n Proto's
# "ordinals must be sequential" requirement is satisfied without dragging
# in echo/mining/chain types.

@0xf2c5cfa319406aa6;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("ipc::capnp::messages");

using Proxy = import "/mp/proxy.capnp";
$Proxy.include("interfaces/init.h");
$Proxy.includeTypes("ipc/capnp/init-types.h");

using Rpc = import "rpc.capnp";
using Mining = import "mining.capnp";

interface Unused $Proxy.wrap("interfaces::Unused") {
    destroy @0 (context :Proxy.Context) -> ();
}

interface Init $Proxy.wrap("interfaces::Init") {
    construct @0 (threadMap: Proxy.ThreadMap) -> (threadMap :Proxy.ThreadMap);
    makeEcho       @1 (context :Proxy.Context) -> (result :Unused);
    makeMiningOld2 @2 () -> ();
    makeMining     @3 (context :Proxy.Context) -> (result :Mining.Mining);
    makeRpc        @4 (context :Proxy.Context) -> (result :Rpc.Rpc);
    makeChain      @5 (context :Proxy.Context) -> (result :Unused);
}
