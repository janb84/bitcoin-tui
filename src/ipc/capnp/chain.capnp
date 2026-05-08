# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Subset of Bitcoin Core's src/ipc/capnp/chain.capnp. Interface ID and
# all 52 ordinals (@0..@51) are preserved verbatim so methods that are
# real here (getBlockHash, havePruned, getPruneHeight, ...) land on the
# same wire slot as upstream and can be invoked against an unmodified
# bitcoin-node IPC server.
#
# Methods whose upstream parameter/return types pull in heavy Bitcoin Core
# machinery (CBlockLocator, FoundBlock, COutPoint/Coin, CFeeRate,
# CTransaction, CRPCCommand, BilingualStr, FeeCalculation, SettingsValue,
# BlockInfo, ChainNotifications, ...) are intentionally STUBBED here:
# their wire format is reduced to `(unused :Bool) -> (result :Bool)` (or
# similar) so we don't need to vendor `primitives/`, `policy/`,
# `rpc/server.h`, etc. bitcoin-tui never calls those methods, so the wire
# divergence on those slots is harmless.
#
# Compared to upstream, the following types have been omitted entirely:
# Common.* (Pair, PairInt64, ResultVoid, FeeCalculation, BilingualStr),
# RPCCommand, JSONRPCRequest, ActorCallback, HelpResult, FoundBlockParam,
# FoundBlockResult, BlockInfo, ChainstateRole, ChainNotifications,
# ChainClient, SettingsUpdateCallback. They are not referenced because
# every method that would have needed them is stubbed.

@0x94f21a4864bd2c65;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("ipc::capnp::messages");

using Proxy = import "/mp/proxy.capnp";
$Proxy.include("interfaces/chain.h");
$Proxy.includeTypes("ipc/capnp/chain-types.h");

using Handler = import "handler.capnp";

interface Chain $Proxy.wrap("interfaces::Chain") {
    destroy                          @0  (context :Proxy.Context) -> ();
    getHeight                        @1  (context :Proxy.Context) -> (result :Int32, hasResult :Bool);
    getBlockHash                     @2  (context :Proxy.Context, height :Int32) -> (result :Data);
    haveBlockOnDisk                  @3  (context :Proxy.Context, height :Int32) -> (result :Bool);
    # @4 STUB — upstream: findLocatorFork(locator :Data)
    findLocatorFork                  @4  (context :Proxy.Context, unused :Bool) -> (result :Bool);
    hasBlockFilterIndex              @5  (context :Proxy.Context, filterType :UInt8) -> (result :Bool);
    # @6 STUB — upstream: blockFilterMatchesAny(filterType, blockHash, filterSet)
    blockFilterMatchesAny            @6  (context :Proxy.Context, unused :Bool) -> (result :Bool);
    # @7..@11 STUB — upstream uses FoundBlockParam/FoundBlockResult
    findBlock                        @7  (context :Proxy.Context, unused :Bool) -> (result :Bool);
    findFirstBlockWithTimeAndHeight  @8  (context :Proxy.Context, unused :Bool) -> (result :Bool);
    findAncestorByHeight             @9  (context :Proxy.Context, unused :Bool) -> (result :Bool);
    findAncestorByHash               @10 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    findCommonAncestor               @11 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    # @12 STUB — upstream: findCoins(coins :List(Common.Pair(Data, Data)))
    findCoins                        @12 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    guessVerificationProgress        @13 (context :Proxy.Context, blockHash :Data) -> (result :Float64);
    hasBlocks                        @14 (context :Proxy.Context, blockHash :Data, minHeight :Int32, maxHeight :Int32, hasMaxHeight :Bool) -> (result :Bool);
    # @15 STUB — upstream: isRBFOptIn(tx :Data)
    isRBFOptIn                       @15 (context :Proxy.Context, unused :Bool) -> (result :Int32);
    isInMempool                      @16 (context :Proxy.Context, txid :Data) -> (result :Bool);
    hasDescendantsInMempool          @17 (context :Proxy.Context, txid :Data) -> (result :Bool);
    # @18 STUB — upstream: broadcastTransaction(tx :Data, maxTxFee :Int64, broadcastMethod :Int32) -> (error :Text, result :Bool)
    broadcastTransaction             @18 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    # @19 STUB — upstream: getTransactionAncestry returns 4 numeric out-params
    getTransactionAncestry           @19 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    # @20..@21 STUB — upstream uses CFeeRate
    calculateIndividualBumpFees      @20 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    calculateCombinedBumpFee         @21 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    getPackageLimits                 @22 (context :Proxy.Context) -> (ancestors :UInt32, descendants :UInt32);
    # @23 STUB — upstream: checkChainLimits returns Common.ResultVoid
    checkChainLimits                 @23 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    # @24 STUB — upstream: estimateSmartFee returns CFeeRate + FeeCalculation
    estimateSmartFee                 @24 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    estimateMaxBlocks                @25 (context :Proxy.Context) -> (result :UInt32);
    # @26..@29 STUB — upstream returns CFeeRate
    mempoolMinFee                    @26 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    relayMinFee                      @27 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    relayIncrementalFee              @28 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    relayDustFee                     @29 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    havePruned                       @30 (context :Proxy.Context) -> (result :Bool);
    getPruneHeight                   @31 (context :Proxy.Context) -> (result :Int32, hasResult :Bool);
    isReadyToBroadcast               @32 (context :Proxy.Context) -> (result :Bool);
    isInitialBlockDownload           @33 (context :Proxy.Context) -> (result :Bool);
    shutdownRequested                @34 (context :Proxy.Context) -> (result :Bool);
    initMessage                      @35 (context :Proxy.Context, message :Text) -> ();
    # @36..@37 STUB — upstream uses Common.BilingualStr
    initWarning                      @36 (context :Proxy.Context, unused :Bool) -> ();
    initError                        @37 (context :Proxy.Context, unused :Bool) -> ();
    showProgress                     @38 (context :Proxy.Context, title :Text, progress :Int32, resumePossible :Bool) -> ();
    # @39 STUB — upstream: handleNotifications(notifications :ChainNotifications)
    handleNotifications              @39 (context :Proxy.Context, unused :Bool) -> (result :Handler.Handler);
    waitForNotificationsIfTipChanged @40 (context :Proxy.Context, oldTip :Data) -> ();
    waitForNotifications             @41 (context :Proxy.Context) -> ();
    # @42 STUB — upstream: handleRpc(command :RPCCommand)
    handleRpc                        @42 (context :Proxy.Context, unused :Bool) -> (result :Handler.Handler);
    rpcEnableDeprecated              @43 (context :Proxy.Context, method :Text) -> (result :Bool);
    # @44..@46 STUB — upstream returns common::SettingsValue (UniValue / JSON)
    getSetting                       @44 (context :Proxy.Context, unused :Bool) -> (result :Text);
    getSettingsList                  @45 (context :Proxy.Context, unused :Bool) -> (result :List(Text));
    getRwSetting                     @46 (context :Proxy.Context, unused :Bool) -> (result :Text);
    # @47..@49 STUB — upstream uses SettingsUpdateCallback / SettingsValue
    updateRwSetting                  @47 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    overwriteRwSetting               @48 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    deleteRwSettings                 @49 (context :Proxy.Context, unused :Bool) -> (result :Bool);
    # @50 STUB — upstream: requestMempoolTransactions(notifications :ChainNotifications)
    requestMempoolTransactions       @50 (context :Proxy.Context, unused :Bool) -> ();
    hasAssumedValidChain             @51 (context :Proxy.Context) -> (result :Bool);
}
