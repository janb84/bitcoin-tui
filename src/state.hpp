#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

// ============================================================================
// Application state (shared between render thread and RPC polling thread)
// ============================================================================
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

    // Status
    std::string last_update;
    std::string error_message;
    bool        connected  = false;
    bool        refreshing = false;
};

struct SoftFork {
    std::string name;
    std::string type; // "buried" | "bip9"
    bool        active = false;
    int64_t     height = -1; // activation height (-1 = unknown)
    // bip9 extras (empty/0 for buried)
    std::string bip9_status;             // defined | started | locked_in | active | failed
    int64_t     bip9_since          = 0; // block height status started
    int64_t     bip9_start_time     = 0; // unix timestamp
    int64_t     bip9_timeout        = 0; // unix timestamp
    int64_t     bip9_min_activation = 0;
    // signalling stats (only present during "started")
    int64_t bip9_elapsed   = 0;
    int64_t bip9_count     = 0;
    int64_t bip9_period    = 0;
    int64_t bip9_threshold = 0;
};

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
