#include "poll.hpp"
#include "format.hpp"

// ============================================================================
// RPC polling
// ============================================================================
void poll_rpc(RpcClient& rpc, Guarded<AppState>& state,
              const std::function<void()>& on_core_ready) {
    try {
        auto bc  = rpc.call("getblockchaininfo")["result"];
        auto net = rpc.call("getnetworkinfo")["result"];
        auto mp  = rpc.call("getmempoolinfo")["result"];

        state.update([&](auto& s) {
            // Blockchain
            s.chain         = bc.value("chain", "—");
            s.blocks        = bc.value("blocks", 0LL);
            s.headers       = bc.value("headers", 0LL);
            s.difficulty    = bc.value("difficulty", 0.0);
            s.progress      = bc.value("verificationprogress", 0.0);
            s.pruned        = bc.value("pruned", false);
            s.ibd           = bc.value("initialblockdownload", false);
            s.bestblockhash = bc.value("bestblockhash", "");

            // Network
            s.connections      = net.value("connections", 0);
            s.connections_in   = net.value("connections_in", 0);
            s.connections_out  = net.value("connections_out", 0);
            s.subversion       = net.value("subversion", "");
            s.protocol_version = net.value("protocolversion", 0);
            s.network_active   = net.value("networkactive", true);
            s.relay_fee        = net.value("relayfee", 0.0);

            // Mempool
            s.mempool_tx      = mp.value("size", 0LL);
            s.mempool_bytes   = mp.value("bytes", 0LL);
            s.mempool_usage   = mp.value("usage", 0LL);
            s.mempool_max     = mp.value("maxmempool", 300000000LL);
            s.mempool_min_fee = mp.value("mempoolminfee", 0.0);
            s.total_fee       = mp.value("total_fee", 0.0);

            // Hashrate derived from difficulty (saves a getmininginfo round-trip):
            // difficulty × 2³² / 600  ≈  expected hashes per second at current difficulty
            s.network_hashps = bc.value("difficulty", 0.0) * 4294967296.0 / 600.0;

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
