#pragma once

#include <functional>
#include <string>

#include "guarded.hpp"
#include "rpc_client.hpp"

// ============================================================================
// What the poll loop learns about the node, shared with the render thread.
//
// This is the chrome around the tabs (chain badge, connection status, refresh
// indicator) and nothing else: every tab is a Lua script that fetches and owns
// its own data, so node details (heights, peers, mempool, …) do not live here.
//
// poll_rpc writes every field except `refreshing`, which main toggles around
// the call so the footer can show a poll in flight.
// ============================================================================
struct NodeStatus {
    std::string chain = "—";

    std::string last_update;
    std::string error_message;
    bool        connected  = false;
    bool        refreshing = false;
};

// Polls the node for host-level state (chain name + reachability), then calls
// on_core_ready. Tab data is fetched by the Lua tabs themselves.
void poll_rpc(RpcClient& rpc, Guarded<NodeStatus>& state,
              const std::function<void()>& on_core_ready = nullptr);
