# Copyright (c) 2021-present The Bitcoin Core developers
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Wire-compatible subset of Bitcoin Core's src/ipc/capnp/init.capnp as
# shipped in v31.0. Preserves the interface ID (@0xf2c5cfa319406aa6) so
# this client can connect to an unmodified v31.0 bitcoin-node IPC server.
# Factory methods (makeMining, ...) are added in follow-up commits as
# bitcoin-tui starts to actually use them.

@0xf2c5cfa319406aa6;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("ipc::capnp::messages");

using Proxy = import "/mp/proxy.capnp";
$Proxy.include("interfaces/init.h");
$Proxy.includeTypes("ipc/capnp/init-types.h");

interface Init $Proxy.wrap("interfaces::Init") {
    construct @0 (threadMap: Proxy.ThreadMap) -> (threadMap :Proxy.ThreadMap);
}
