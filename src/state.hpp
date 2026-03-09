#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// Block animation parameters
// ============================================================================
static constexpr int BLOCK_ANIM_SLIDE_FRAMES = 12; // frames sliding right (~480 ms)
static constexpr int BLOCK_ANIM_TOTAL_FRAMES = BLOCK_ANIM_SLIDE_FRAMES;

// ============================================================================
// Application state (shared between render thread and RPC polling thread)
// ============================================================================
struct BlockStat {
    int64_t height       = 0;
    int64_t txs          = 0;
    int64_t total_size   = 0;
    int64_t total_weight = 0;
    int64_t time         = 0;
};

struct PeerInfo {
    int         id = 0;
    std::string addr;
    std::string network;
    std::string subver;
    bool        inbound       = false;
    int64_t     bytes_sent    = 0;
    int64_t     bytes_recv    = 0;
    double      ping_ms       = -1.0;
    int         version       = 0;
    int64_t     synced_blocks = 0;
    int64_t     conntime      = 0;
    std::string services;
    int64_t     startingheight = 0;
    bool        bip152_hb_from = false;
    bool        bip152_hb_to   = false;
    std::string connection_type;
    std::string transport;
    int64_t     addr_processed = 0;
    double      min_ping_ms    = -1.0;
};

struct AppState {
    // Blockchain
    std::string chain      = "—";
    int64_t     blocks     = 0;
    int64_t     headers    = 0;
    double      difficulty = 0.0;
    double      progress   = 0.0;
    bool        pruned     = false;
    bool        ibd        = false;
    std::string bestblockhash;

    // Network
    int         connections     = 0;
    int         connections_in  = 0;
    int         connections_out = 0;
    std::string subversion;
    int         protocol_version = 0;
    bool        network_active   = true;
    double      relay_fee        = 0.0;

    // Mempool
    int64_t mempool_tx      = 0;
    int64_t mempool_bytes   = 0;
    int64_t mempool_usage   = 0;
    int64_t mempool_max     = 300000000;
    double  mempool_min_fee = 0.0;
    double  total_fee       = 0.0;

    // Mining
    double network_hashps = 0.0;

    // Peers
    std::vector<PeerInfo> peers;

    // Recent blocks (index 0 = newest, populated by getblockstats)
    std::vector<BlockStat> recent_blocks;
    int64_t                blocks_fetched_at = -1;

    // Block animation
    bool                   block_anim_active = false;
    int                    block_anim_frame  = 0;
    std::vector<BlockStat> block_anim_old; // snapshot before new block arrived

    // Status
    std::string last_update;
    std::string error_message;
    bool        connected  = false;
    bool        refreshing = false;

    // Private broadcast queue (Bitcoin Core PR #29415)
    std::vector<std::string> privbcast_txids;
};

struct TxVin {
    std::string txid;
    int         vout        = 0;
    bool        is_coinbase = false;
};

struct TxVout {
    double      value = 0.0;
    std::string address; // may be empty for non-standard scripts
    std::string type;    // scriptPubKey type
};

struct TxSearchState {
    std::string txid;
    bool        searching = false;
    bool        found     = false;
    bool        is_block  = false; // true = block result, false = tx result
    bool        confirmed = false; // tx only: true = in a block, false = in mempool
    std::string error;
    // Shared (tx)
    int64_t vsize  = 0;
    int64_t weight = 0;
    // Mempool-only
    double  fee         = 0.0; // BTC
    double  fee_rate    = 0.0; // sat/vB
    int64_t ancestors   = 0;
    int64_t descendants = 0;
    int64_t entry_time  = 0;
    // Confirmed tx-only
    std::string blockhash;
    int64_t     block_height  = -1;
    int64_t     confirmations = 0;
    int64_t     blocktime     = 0;
    int         vin_count     = 0;
    int         vout_count    = 0;
    double      total_output  = 0.0; // BTC, sum of all outputs
    // Block result fields
    std::string blk_hash;
    int64_t     blk_height     = 0;
    int64_t     blk_time       = 0;
    int64_t     blk_ntx        = 0;
    int64_t     blk_size       = 0;
    int64_t     blk_weight     = 0;
    double      blk_difficulty = 0.0;
    std::string blk_miner;
    int64_t     blk_confirmations = 0;
    // Input/output navigation
    std::vector<TxVin>  vin_list;
    std::vector<TxVout> vout_list;
    int                 io_selected = -1;
    // Inputs overlay (opened by pressing Enter on the Inputs row)
    bool inputs_overlay_open = false;
    int  input_overlay_sel   = -1;
    // Outputs overlay (opened by pressing Enter on the Outputs row)
    bool outputs_overlay_open = false;
    int  output_overlay_sel   = -1;
};

struct BroadcastState {
    std::string hex;
    bool        submitting = false;
    std::string result_txid;
    std::string result_error;
    bool        success    = false;
    bool        has_result = false;
};

struct AddNodeState {
    int         cmd_idx    = 0; // 0=onetry, 1=add, 2=remove
    bool        pending    = false;
    bool        has_result = false;
    bool        success    = false;
    std::string result_message;
};

struct BanNodeState {
    bool        is_remove  = false; // false=ban, true=unban
    bool        pending    = false;
    bool        has_result = false;
    bool        success    = false;
    std::string result_message;
};

struct PeerActionResult {
    bool        has_result = false;
    bool        success    = false;
    std::string message;
};

struct AddedNodeInfo {
    std::string addednode;
    bool        connected = false;
};

struct BannedEntry {
    std::string address;
    int64_t     banned_until = 0;
    std::string ban_reason;
};

// ============================================================================
// Result classification and navigation helpers (pure, inline)
// ============================================================================
enum class TxResultKind { Searching, Block, Mempool, Confirmed, Error };

inline TxResultKind classify_result(const TxSearchState& ss) {
    if (ss.searching)
        return TxResultKind::Searching;
    if (!ss.found)
        return TxResultKind::Error;
    if (ss.is_block)
        return TxResultKind::Block;
    return ss.confirmed ? TxResultKind::Confirmed : TxResultKind::Mempool;
}

// io_selected: -1=none, 0=block row, 1=inputs(if any), 1or2=outputs(if any)
inline int io_inputs_idx(const TxSearchState& ss) { return ss.vin_list.empty() ? -1 : 1; }
inline int io_outputs_idx(const TxSearchState& ss) {
    if (ss.vout_list.empty())
        return -1;
    return ss.vin_list.empty() ? 1 : 2;
}
inline int io_max_sel(const TxSearchState& ss) {
    int n = 0;
    if (!ss.vin_list.empty())
        n++;
    if (!ss.vout_list.empty())
        n++;
    return n;
}

// Query validators — pure predicates.
inline bool is_txid(const std::string& s) {
    if (s.size() != 64)
        return false;
    return std::ranges::all_of(s, [](unsigned char c) { return std::isxdigit(c) != 0; });
}
inline bool is_height(const std::string& s) {
    return !s.empty() && s.size() <= 8 &&
           std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c) != 0; });
}
