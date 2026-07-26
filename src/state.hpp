#pragma once

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
