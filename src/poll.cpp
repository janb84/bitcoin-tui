#include <vector>

#include "format.hpp"
#include "poll.hpp"

// ============================================================================
// RPC polling
// ============================================================================
void poll_rpc(RpcClient& rpc, AppState& state, std::mutex& mtx,
              const std::function<void()>& on_core_ready) {
    // Read cached tip height so we can skip re-fetching block stats when tip hasn't moved.
    int64_t cached_tip = 0;
    {
        std::lock_guard lk(mtx);
        cached_tip = state.blocks_fetched_at;
    }

    try {
        // ── Phase 1: fast calls ──────────────────────────────────────────────
        auto bc  = rpc.call("getblockchaininfo")["result"];
        auto net = rpc.call("getnetworkinfo")["result"];
        auto mp  = rpc.call("getmempoolinfo")["result"];
        auto pi  = rpc.call("getpeerinfo")["result"];

        int64_t new_tip = bc.value("blocks", 0LL);

        // Commit core state immediately so the UI can render before block stats arrive.
        {
            std::lock_guard lock(mtx);

            // Blockchain
            state.chain         = bc.value("chain", "—");
            state.blocks        = bc.value("blocks", 0LL);
            state.headers       = bc.value("headers", 0LL);
            state.difficulty    = bc.value("difficulty", 0.0);
            state.progress      = bc.value("verificationprogress", 0.0);
            state.pruned        = bc.value("pruned", false);
            state.ibd           = bc.value("initialblockdownload", false);
            state.bestblockhash = bc.value("bestblockhash", "");

            // Network
            state.connections      = net.value("connections", 0);
            state.connections_in   = net.value("connections_in", 0);
            state.connections_out  = net.value("connections_out", 0);
            state.subversion       = net.value("subversion", "");
            state.protocol_version = net.value("protocolversion", 0);
            state.network_active   = net.value("networkactive", true);
            state.relay_fee        = net.value("relayfee", 0.0);

            // Mempool
            state.mempool_tx      = mp.value("size", 0LL);
            state.mempool_bytes   = mp.value("bytes", 0LL);
            state.mempool_usage   = mp.value("usage", 0LL);
            state.mempool_max     = mp.value("maxmempool", 300000000LL);
            state.mempool_min_fee = mp.value("mempoolminfee", 0.0);
            state.total_fee       = mp.value("total_fee", 0.0);

            // Hashrate derived from difficulty (saves a getmininginfo round-trip):
            // difficulty × 2³² / 600  ≈  expected hashes per second at current difficulty
            state.network_hashps = bc.value("difficulty", 0.0) * 4294967296.0 / 600.0;

            // Peers
            state.peers.clear();
            for (const auto& p : pi) {
                PeerInfo peer;
                peer.id            = p.value("id", 0);
                peer.addr          = p.value("addr", "");
                peer.network       = p.value("network", "");
                peer.subver        = p.value("subver", "");
                peer.inbound       = p.value("inbound", false);
                peer.bytes_sent    = p.value("bytessent", 0LL);
                peer.bytes_recv    = p.value("bytesrecv", 0LL);
                peer.version       = p.value("version", 0);
                peer.synced_blocks = p.value("synced_blocks", 0LL);
                if (p.contains("pingtime") && p["pingtime"].is_number()) {
                    peer.ping_ms = p["pingtime"].get<double>() * 1000.0;
                }
                state.peers.push_back(std::move(peer));
            }

            state.connected = true;
            state.error_message.clear();
            state.last_update = now_string();
        }

        // Private broadcast queue (Bitcoin Core PR #29415 — skipped on older nodes)
        try {
            auto                     pbinfo = rpc.call("getprivatebroadcastinfo")["result"];
            std::vector<std::string> txids;
            if (pbinfo.is_array()) {
                for (const auto& entry : pbinfo) {
                    if (entry.is_string())
                        txids.push_back(entry.get<std::string>());
                    else if (entry.is_object() && entry.contains("txid"))
                        txids.push_back(entry["txid"].get<std::string>());
                }
            }
            {
                std::lock_guard lock(mtx);
                state.privbcast_txids = std::move(txids);
            }
        } catch (...) {
        }

        // Let the UI render with core data while block stats are fetched.
        if (on_core_ready)
            on_core_ready();

        // ── Phase 2: per-block stats (slow — 7 sequential calls) ────────────
        if (new_tip != cached_tip && new_tip > 0) {
            std::vector<BlockStat> fresh_blocks;
            for (int i = 0; i < 7 && (new_tip - i) >= 0; ++i) {
                try {
                    json      params = {new_tip - i,
                                        json({"height", "txs", "total_size", "total_weight"})};
                    auto      bs     = rpc.call("getblockstats", params)["result"];
                    BlockStat blk;
                    blk.height       = bs.value("height", 0LL);
                    blk.txs          = bs.value("txs", 0LL);
                    blk.total_size   = bs.value("total_size", 0LL);
                    blk.total_weight = bs.value("total_weight", 0LL);
                    fresh_blocks.push_back(blk);
                } catch (...) {
                    break;
                }
            }

            std::lock_guard lock(mtx);
            // Trigger slide animation when a new block arrives.
            if (!state.recent_blocks.empty() && !fresh_blocks.empty()) {
                state.block_anim_old    = state.recent_blocks;
                state.block_anim_frame  = 0;
                state.block_anim_active = true;
            }
            state.recent_blocks     = std::move(fresh_blocks);
            state.blocks_fetched_at = new_tip;
        }

    } catch (const std::exception& e) {
        std::lock_guard lock(mtx);
        state.connected     = false;
        state.error_message = e.what();
        state.last_update   = now_string();
    }
}
