// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_CHAIN_H
#define BITCOIN_TUI_INTERFACES_CHAIN_H

#include "handler.h"
#include "types.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace interfaces {

//! Stand-in for Bitcoin Core's interfaces::Chain. The 52 ordinals (@0..@51)
//! match upstream's src/interfaces/chain.h and src/ipc/capnp/chain.capnp so
//! the wire layout of `ProxyClient<Chain>` lines up with an unmodified
//! bitcoin-node IPC server.
//!
//! Methods whose upstream signature pulls in heavy Bitcoin Core types
//! (CBlockLocator, FoundBlock, COutPoint/Coin, CFeeRate, CTransaction,
//! CRPCCommand, BilingualStr, FeeCalculation, SettingsValue, BlockInfo, ...)
//! are intentionally STUBBED — their parameter/return types are simplified
//! to primitives (typically `bool`) in both this header and the matching
//! capnp schema. bitcoin-tui never invokes those methods, so the wire
//! format on those slots is allowed to diverge from upstream. The slot
//! itself still exists so generated `ProxyClient<Chain>` overrides land on
//! the correct ordinals for the real methods (getBlockHash, havePruned,
//! getPruneHeight, …).
//!
//! This is deliberately bulky: it's a proof-of-concept exercising whether
//! the Chain interface is sufficient for a TUI without vendoring half of
//! Bitcoin Core's primitive layer. The set of stubbed methods is itself
//! the answer.
class Chain
{
public:
    virtual ~Chain() = default;

    // ---- Real methods (callable over IPC) ----------------------------------

    //! @1  Current chain height (excludes genesis); nullopt for empty chain.
    virtual std::optional<int> getHeight() = 0;

    //! @2  Block hash by height. Aborts node-side if height invalid.
    virtual uint256 getBlockHash(int height) = 0;

    //! @3  True if block data is on disk (not pruned).
    virtual bool haveBlockOnDisk(int height) = 0;

    //! @5  True if the requested block-filter index is built.
    virtual bool hasBlockFilterIndex(uint8_t filter_type) = 0;

    //! @13 Estimated fraction of total transactions verified up to block.
    virtual double guessVerificationProgress(const uint256& block_hash) = 0;

    //! @14 Whether all blocks in [min_height,max_height] (max optional) on
    //!     the chain ending at block_hash are available locally.
    virtual bool hasBlocks(const uint256& block_hash, int min_height = 0,
                           std::optional<int> max_height = {}) = 0;

    //! @16 True if txid is currently in the mempool.
    virtual bool isInMempool(const uint256& txid) = 0;

    //! @17 True if txid has any descendants in the mempool.
    virtual bool hasDescendantsInMempool(const uint256& txid) = 0;

    //! @22 Mempool ancestor/descendant package limits.
    virtual void getPackageLimits(unsigned int& limit_ancestor_count,
                                  unsigned int& limit_descendant_count) = 0;

    //! @25 Fee-estimator maximum target (in blocks).
    virtual unsigned int estimateMaxBlocks() = 0;

    //! @30 True if any block has been pruned.
    virtual bool havePruned() = 0;

    //! @31 Current prune height, or nullopt if not pruning.
    virtual std::optional<int> getPruneHeight() = 0;

    //! @32 True if the node is ready to broadcast transactions.
    virtual bool isReadyToBroadcast() = 0;

    //! @33 True while in initial block download.
    virtual bool isInitialBlockDownload() = 0;

    //! @34 True if shutdown has been requested.
    virtual bool shutdownRequested() = 0;

    //! @35 Forward an init progress message.
    virtual void initMessage(const std::string& message) = 0;

    //! @38 Forward a generic progress indicator.
    virtual void showProgress(const std::string& title, int progress,
                              bool resume_possible) = 0;

    //! @40 Block until pending notifications drain, unless old_tip already
    //!     matches the current chain tip.
    virtual void waitForNotificationsIfTipChanged(const uint256& old_tip) = 0;

    //! @41 Block until all pending notifications have been processed.
    virtual void waitForNotifications() = 0;

    //! @43 Whether a deprecated RPC method is currently enabled.
    virtual bool rpcEnableDeprecated(const std::string& method) = 0;

    //! @51 True if the active chain uses an assumed-valid snapshot.
    virtual bool hasAssumedValidChain() = 0;

    // ---- Stub methods (slot reserved for ordinal layout, not callable) -----
    //
    // Each method below corresponds to an upstream Chain method whose true
    // signature involves Core types we don't want to vendor. The wire
    // format on these slots is reduced to `(unused :Bool) -> (result :Bool)`
    // (or similar) in chain.capnp. Calling any of them throws.

    virtual bool findLocatorFork(bool /*unused*/)                              = 0; // @4
    virtual bool blockFilterMatchesAny(bool /*unused*/)                        = 0; // @6
    virtual bool findBlock(bool /*unused*/)                                    = 0; // @7
    virtual bool findFirstBlockWithTimeAndHeight(bool /*unused*/)              = 0; // @8
    virtual bool findAncestorByHeight(bool /*unused*/)                         = 0; // @9
    virtual bool findAncestorByHash(bool /*unused*/)                           = 0; // @10
    virtual bool findCommonAncestor(bool /*unused*/)                           = 0; // @11
    virtual bool findCoins(bool /*unused*/)                                    = 0; // @12
    virtual int  isRBFOptIn(bool /*unused*/)                                   = 0; // @15
    virtual bool broadcastTransaction(bool /*unused*/)                         = 0; // @18
    virtual bool getTransactionAncestry(bool /*unused*/)                       = 0; // @19
    virtual bool calculateIndividualBumpFees(bool /*unused*/)                  = 0; // @20
    virtual bool calculateCombinedBumpFee(bool /*unused*/)                     = 0; // @21
    virtual bool checkChainLimits(bool /*unused*/)                             = 0; // @23
    virtual bool estimateSmartFee(bool /*unused*/)                             = 0; // @24
    virtual bool mempoolMinFee(bool /*unused*/)                                = 0; // @26
    virtual bool relayMinFee(bool /*unused*/)                                  = 0; // @27
    virtual bool relayIncrementalFee(bool /*unused*/)                          = 0; // @28
    virtual bool relayDustFee(bool /*unused*/)                                 = 0; // @29
    virtual void initWarning(bool /*unused*/)                                  = 0; // @36
    virtual void initError(bool /*unused*/)                                    = 0; // @37
    virtual std::unique_ptr<Handler> handleNotifications(bool /*unused*/)      = 0; // @39
    virtual std::unique_ptr<Handler> handleRpc(bool /*unused*/)                = 0; // @42
    virtual std::string              getSetting(bool /*unused*/)               = 0; // @44
    virtual std::vector<std::string> getSettingsList(bool /*unused*/)          = 0; // @45
    virtual std::string              getRwSetting(bool /*unused*/)             = 0; // @46
    virtual bool updateRwSetting(bool /*unused*/)                              = 0; // @47
    virtual bool overwriteRwSetting(bool /*unused*/)                           = 0; // @48
    virtual bool deleteRwSettings(bool /*unused*/)                             = 0; // @49
    virtual void requestMempoolTransactions(bool /*unused*/)                   = 0; // @50
};

} // namespace interfaces

#endif // BITCOIN_TUI_INTERFACES_CHAIN_H
