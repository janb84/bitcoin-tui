# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Wire-compatible subset of Bitcoin Core's src/ipc/capnp/mining.capnp.
# Only ordinals @0..@3 and @6 are used by bitcoin-tui; @4 and @5 are
# placeholder stubs (createNewBlock / checkBlock upstream) declared with
# trivial primitive signatures so the C++ ProxyClient<Mining> still has all
# its virtual methods overridden. We never invoke @4/@5, so the wire format
# for those slots can diverge safely.

@0xc77d03df6a41b505;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("ipc::capnp::messages");

using Proxy = import "/mp/proxy.capnp";
$Proxy.include("interfaces/mining.h");
$Proxy.includeTypes("ipc/capnp/mining-types.h");

# 1:1 wrap of interfaces::BlockRef. The C++ struct exposes `hash` and
# `height` fields by exactly these names, which is what $Proxy.wrap requires.
struct BlockRef $Proxy.wrap("interfaces::BlockRef") {
    hash   @0 :Data;
    height @1 :Int32;
}

interface Mining $Proxy.wrap("interfaces::Mining") {
    isTestChain            @0 (context :Proxy.Context) -> (result :Bool);
    isInitialBlockDownload @1 (context :Proxy.Context) -> (result :Bool);
    getTip                 @2 (context :Proxy.Context) -> (result :BlockRef, hasResult :Bool);
    waitTipChanged         @3 (context :Proxy.Context, currentTip :Data, timeout :Float64)
                                                       -> (result :BlockRef, hasResult :Bool);
    # Stubs — never called by bitcoin-tui.
    createNewBlock         @4 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    checkBlock             @5 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    interrupt              @6 () -> ();
}
