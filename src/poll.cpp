#include <vector>

#include "format.hpp"
#include "poll.hpp"

// ============================================================================
// RPC polling
// ============================================================================
void poll_rpc(RpcClient& rpc, Guarded<AppState>& state,
              const std::function<void()>& on_core_ready) {
    // Read cached tip height so we can skip re-fetching block stats when tip hasn't moved.
    int64_t cached_tip = state.access([](const auto& s) { return s.blocks_fetched_at; });

    try {
        // ── Phase 1: fast calls ──────────────────────────────────────────────
        auto bc  = rpc.call("getblockchaininfo")["result"];
        auto net = rpc.call("getnetworkinfo")["result"];
        auto mp  = rpc.call("getmempoolinfo")["result"];

        int64_t new_tip = bc.value("blocks", 0LL);

        // Commit core state immediately so the UI can render before block stats arrive.
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

        // Let the UI render with core data while block stats are fetched.
        if (on_core_ready)
            on_core_ready();

        // ── Phase 2: per-block stats (slow — up to 20 sequential calls) ────────────
        if (new_tip != cached_tip && new_tip > 0) {
            std::vector<BlockStat> fresh_blocks;
            for (int i = 0; i < 20 && (new_tip - i) >= 0; ++i) {
                try {
                    json      params = {new_tip - i,
                                        json({"height", "txs", "total_size", "total_weight", "time"})};
                    auto      bs     = rpc.call("getblockstats", params)["result"];
                    BlockStat blk;
                    blk.height       = bs.value("height", 0LL);
                    blk.txs          = bs.value("txs", 0LL);
                    blk.total_size   = bs.value("total_size", 0LL);
                    blk.total_weight = bs.value("total_weight", 0LL);
                    blk.time         = bs.value("time", 0LL);
                    fresh_blocks.push_back(blk);
                } catch (...) {
                    break;
                }
            }

            state.update([&](auto& s) {
                // Trigger slide animation when a new block arrives.
                if (!s.recent_blocks.empty() && !fresh_blocks.empty()) {
                    s.block_anim_old    = s.recent_blocks;
                    s.block_anim_frame  = 0;
                    s.block_anim_active = true;
                }
                s.recent_blocks     = std::move(fresh_blocks);
                s.blocks_fetched_at = new_tip;
            });
        }

    } catch (const std::exception& e) {
        state.update([&](auto& s) {
            s.connected     = false;
            s.error_message = e.what();
            s.last_update   = now_string();
        });
    }
}
