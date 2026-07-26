#pragma once

#include <algorithm>
#include <cctype>
#include <string>

// ============================================================================
// Application state (shared between render thread and RPC polling thread)
//
// Host-level state only: the chrome around the tabs (chain badge, connection
// status, refresh indicator). Every tab is a Lua script that fetches and owns
// its own data, so node details (heights, peers, mempool, …) do not live here.
// ============================================================================
struct AppState {
    std::string chain = "—";

    std::string last_update;
    std::string error_message;
    bool        connected  = false;
    bool        refreshing = false;
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
