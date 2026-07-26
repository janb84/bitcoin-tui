#pragma once

#include <functional>
#include <mutex>

#include "guarded.hpp"
#include "rpc_client.hpp"
#include "state.hpp"

// Polls the node for host-level state (chain name + reachability), then calls
// on_core_ready. Tab data is fetched by the Lua tabs themselves.
void poll_rpc(RpcClient& rpc, Guarded<AppState>& state,
              const std::function<void()>& on_core_ready = nullptr);
