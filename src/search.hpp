#pragma once

#include <cstdint>

#include "rpc_client.hpp"
#include "state.hpp"

// Pure transaction/block lookup — no shared state, no UI side-effects.
// query_is_height: true when query is a decimal block height string.
TxSearchState perform_tx_search(const RpcConfig& cfg, const std::string& query,
                                bool query_is_height, int64_t tip);
