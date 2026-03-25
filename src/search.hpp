#pragma once

#include <cstdint>

#include "rpc_client.hpp"
#include "state.hpp"

// Pure transaction/block lookup — no shared state, no UI side-effects.
// query_is_height: true when query is a decimal block height string.
// blockhash_hint: if non-empty, passed to getrawtransaction to avoid needing txindex.
TxSearchState perform_tx_search(const RpcConfig& cfg, const RpcAuth& auth, const std::string& query,
                                bool query_is_height, int64_t tip,
                                const std::string& blockhash_hint = "");

// Fetch detailed block data (verbosity 3) with tx list and fees, for Explorer browse.
TxSearchState perform_block_search(const RpcConfig& cfg, const RpcAuth& auth, int64_t height);

// Fetch the current mempool tx list via getrawmempool.
TxSearchState perform_mempool_search(const RpcConfig& cfg, const RpcAuth& auth);
