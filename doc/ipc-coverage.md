# IPC vs JSON-RPC coverage

bitcoin-tui has three call paths to bitcoin-node:

1. **Typed IPC** — native Cap'n Proto methods on `interfaces::Mining` /
   `interfaces::Chain`, dispatched through the libmultiprocess proxy.
   No JSON parsing on either end.
2. **JSON-over-IPC** — the same unix socket, but using
   `interfaces::Rpc::executeRpc(json_string)` as a generic transport.
   The wire payload is a JSON-RPC request/response string; this is
   how every bitcoind RPC method that doesn't have a Chain/Mining
   accessor gets reached when an IPC socket is available.
3. **HTTP JSON-RPC** — classic cookie-authenticated HTTP. Used as
   fallback when no IPC socket is present (or when bitcoin-tui was
   built with `-DWITH_IPC=OFF`).

The transport indicator in the status bar shows which combination is
active (`via IPC (typed: Mining,Chain)`, `via IPC`, or `via HTTP`).

## What goes over which path

### Typed IPC (Cap'n Proto)

| AppState field / use site            | Method                                  | Capnp ord. | RPC fallback                |
| ------------------------------------ | --------------------------------------- | ---------- | --------------------------- |
| `blocks` (chain height)              | `Chain.getHeight`                       | @1         | `getblockchaininfo.blocks`  |
| `pruned`                             | `Chain.havePruned`                      | @30        | `getblockchaininfo.pruned`  |
| `prune_height`                       | `Chain.getPruneHeight`                  | @31        | `getblockchaininfo.pruneheight` |
| `ibd` (initial block download)       | `Chain.isInitialBlockDownload`          | @33        | `getblockchaininfo.initialblockdownload` |
| `progress` (verification progress)   | `Chain.guessVerificationProgress(tip)`  | @13        | `getblockchaininfo.verificationprogress` |
| `assumed_valid`                      | `Chain.hasAssumedValidChain`            | @51        | `getblockchaininfo.hasassumedvalidchain` |
| `test_chain` (testnet/regtest/signet)| `Mining.isTestChain`                    | @0         | `getblockchaininfo.chain != "main"` |
| Tip-change wakeup                    | `Mining.waitTipChanged`                 | @3         | poll `getblockchaininfo` every `--refresh` seconds |

The typed Mining/Chain calls are issued *in addition to* the JSON poll
that produced the same data — they're a cross-check today, and the
foundation for dropping those JSON calls outright once enough fields
are covered. `Mining.waitTipChanged` is already authoritative: when it
fires, the JSON poll runs immediately and the periodic refresh loop is
suspended.

### JSON over IPC (or HTTP fallback)

These currently have no Chain/Mining accessor and so always go through
`Rpc.executeRpc` (typed-IPC tunnel) or HTTP, depending on transport.

| Source                      | RPC method                | Used for                                                                         |
| --------------------------- | ------------------------- | -------------------------------------------------------------------------------- |
| [src/poll.cpp](../src/poll.cpp)             | `getblockchaininfo`       | `chain`, `headers`, `difficulty`, `time`, `bestblockhash`, soft-fork map; cross-checked by Chain typed calls above |
| [src/poll.cpp](../src/poll.cpp)             | `getnetworkinfo`          | `connections{,_in,_out}`, `subversion`, `protocol_version`, `network_active`, `relay_fee` |
| [src/poll.cpp](../src/poll.cpp)             | `getmempoolinfo`          | `mempool_tx`, `mempool_bytes`, `mempool_usage`, `mempool_max`, `mempool_min_fee`, `total_fee` |
| [src/poll.cpp](../src/poll.cpp)             | `getpeerinfo`             | per-peer stats for the Peers tab                                                 |
| [src/poll.cpp](../src/poll.cpp)             | `getblockstats`           | last 20 blocks for the dashboard mini-chart                                      |
| [src/poll.cpp](../src/poll.cpp)             | `getprivatebroadcastinfo` | broadcast queue (PR #29415; absent on older nodes — failure is silent)           |
| [src/search.cpp](../src/search.cpp)           | `getblockhash`            | block-height search                                                              |
| [src/search.cpp](../src/search.cpp)           | `getblock`                | search drill-down                                                                |
| [src/search.cpp](../src/search.cpp)           | `getrawtransaction`       | tx search drill-down                                                             |
| [src/search.cpp](../src/search.cpp)           | `getmempoolentry`         | mempool tx search                                                                |
| [src/tabs/tools.cpp](../src/tabs/tools.cpp)       | `sendrawtransaction`      | broadcast UI                                                                     |
| [src/tabs/tools.cpp](../src/tabs/tools.cpp)       | `stop`                    | shutdown button                                                                  |
| [src/tabs/luatab.cpp](../src/tabs/luatab.cpp)      | (any)                     | user lua scripts can call any RPC                                                |

## Why these are still JSON

bitcoin-node's `Init` capability factory only exposes four interfaces:
`Echo`, `Mining`, `Rpc`, and `Chain`. Crucially, `interfaces::Node` —
which owns `getNetworkInfo`, `getNodeStats`, `getMempoolSize`,
`getMempoolDynamicUsage`, `getBestBlockHash`, `broadcastTransaction`,
`startShutdown` and friends — is **not** wired into the IPC server.
That makes everything network-, peer-, and mempool-statistics-related
unreachable as typed IPC; it has to go through `Rpc.executeRpc`.

A few JSON RPCs *do* have a Chain accessor that would in principle let
us bypass JSON, but un-stubbing them in our schema requires vendoring
heavy Bitcoin Core types:

| RPC                  | Typed equivalent          | Cost to un-stub                                                          |
| -------------------- | ------------------------- | ------------------------------------------------------------------------ |
| `getblock`           | `Chain.findBlock` @7      | `FoundBlockParam`/`FoundBlockResult` adapters + serialize `CBlock`       |
| `sendrawtransaction` | `Chain.broadcastTransaction` @18 | deserialize `CTransactionRef` (drag in `primitives/transaction.h` etc.)  |
| `getblockhash`       | `Chain.getBlockHash` @2   | already real on the wire — could swap call site (small win)              |
| `mempoolMinFee`      | `Chain.mempoolMinFee` @26 | `CFeeRate` + 8-byte LE adapter                                           |

For a TUI the cost/benefit doesn't favour bucket-2 (`getblock` /
`sendrawtransaction`); JSON-over-IPC is already real IPC and avoids
bonding our build to Core's primitive layer.

## Proof-of-concept finding

Everything actually useful for a monitoring TUI that doesn't have a
typed Chain accessor lives behind `interfaces::Node`. The takeaway for
upstream is that exposing Node over IPC (or splitting it into smaller
focused interfaces — `Network`, `MempoolStats`, `Lifecycle`) would let
clients like bitcoin-tui drop JSON entirely. The current Chain-only
surface is sufficient for header-of-chain queries but not for any
network/peer/mempool dashboard.
