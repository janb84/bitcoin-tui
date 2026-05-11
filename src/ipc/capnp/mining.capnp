# Copyright (c) 2024-present The Bitcoin Core developers
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Wire-compatible subset of Bitcoin Core's src/ipc/capnp/mining.capnp,
# matching the schema shipped in v31.0 method-by-method (ordinals @0..@6
# and the same field names / types). Only @2 (getTip), @3 (waitTipChanged),
# and @6 (interrupt) are actually called by bitcoin-tui; @4 (createNewBlock)
# and @5 (checkBlock) are wired with stub argument/return types so the
# generated ProxyClient<Mining> still has all of its virtual methods
# overridden — bitcoin-tui never invokes those slots, so that wire format
# can diverge safely.

@0xc77d03df6a41b505;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("ipc::capnp::messages");

using Common = import "common.capnp";
using Proxy = import "/mp/proxy.capnp";
$Proxy.include("interfaces/mining.h");
$Proxy.includeTypes("ipc/capnp/mining-types.h");

const maxDouble :Float64 = 1.7976931348623157e308;

interface Mining $Proxy.wrap("interfaces::Mining") {
    isTestChain            @0 (context :Proxy.Context) -> (result :Bool);
    isInitialBlockDownload @1 (context :Proxy.Context) -> (result :Bool);
    getTip                 @2 (context :Proxy.Context) -> (result :Common.BlockRef, hasResult :Bool);
    waitTipChanged         @3 (context :Proxy.Context, currentTip :Data, timeout :Float64 = .maxDouble)
                                                       -> (result :Common.BlockRef);
    # Stubs — never called by bitcoin-tui.
    createNewBlock         @4 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    checkBlock             @5 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    interrupt              @6 () -> ();
}
