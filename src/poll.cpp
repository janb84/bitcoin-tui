#include "poll.hpp"
#include "format.hpp"

// ============================================================================
// RPC polling
// ============================================================================
void poll_rpc(RpcClient& rpc, Guarded<AppState>& state,
              const std::function<void()>& on_core_ready) {
    try {
        // One call: it doubles as the reachability probe for the connection overlay
        // and supplies the chain name for the header badge. Everything else a tab
        // shows, that tab fetches itself from Lua.
        auto bc = rpc.call("getblockchaininfo")["result"];

        state.update([&](auto& s) {
            s.chain     = bc.value("chain", "—");
            s.connected = true;
            s.error_message.clear();
            s.last_update = now_string();
        });

        if (on_core_ready)
            on_core_ready();
    } catch (const std::exception& e) {
        state.update([&](auto& s) {
            s.connected     = false;
            s.error_message = e.what();
            s.last_update   = now_string();
        });
    }
}
