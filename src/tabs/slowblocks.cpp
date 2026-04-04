#include "slowblocks.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>

#include "logwatcher.hpp"
#include "render.hpp"

using namespace ftxui;
using Clock     = std::chrono::system_clock;
using TimePoint = Clock::time_point;
using namespace std::chrono_literals;

namespace {

double to_seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

Color block_delay_color(double seconds) {
    if (seconds < 1)
        return Color::Green;
    if (seconds < 10)
        return Color::Yellow;
    if (seconds < 60)
        return Color(uint8_t(255), uint8_t(165), uint8_t(0)); // orange
    return Color::Red;
}

Color validation_color(double ms) {
    if (ms < 100)
        return Color::Green;
    if (ms < 1000)
        return Color::Yellow;
    if (ms < 10000)
        return Color(uint8_t(255), uint8_t(165), uint8_t(0)); // orange
    return Color::Red;
}

std::string fmt_validation_time(double ms) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3fs", ms / 1000.0);
    return buf;
}

std::string fmt_size(int bytes) {
    if (bytes < 1000000) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fKB", bytes / 1000.0);
        return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2fMB", bytes / 1000000.0);
    return buf;
}

std::string fmt_time(TimePoint tp) {
    auto    tt = Clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1'000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03ld", tm.tm_hour, tm.tm_min, tm.tm_sec,
             static_cast<long>(ms.count()));
    return buf;
}

std::string fmt_delta(double seconds) {
    char buf[32];
    if (seconds < 60) {
        snprintf(buf, sizeof(buf), "%.3fs", seconds);
    } else {
        int    mins = static_cast<int>(seconds) / 60;
        double secs = seconds - mins * 60;
        snprintf(buf, sizeof(buf), "%dm %.0fs", mins, secs);
    }
    return buf;
}

std::string fmt_compact(const BlockEvent& b) {
    if (!b.time_block)
        return "";
    if (!b.via_compact)
        return "no";
    if (b.txns_requested > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "yes (%d requested)", b.txns_requested);
        return buf;
    }
    return "yes";
}

std::string abbrev_hash(const std::string& hash, size_t prefix = 12, size_t suffix = 8) {
    if (hash.size() <= prefix + suffix + 3)
        return hash;
    return hash.substr(0, prefix) + "..." + hash.substr(hash.size() - suffix);
}

// Fetch chain tips, keeping only active + those whose tip hash appears in known set
std::vector<ChainTipInfo> fetch_tips(RpcClient& rpc, const std::set<std::string>& known) {
    std::vector<ChainTipInfo> tips;
    try {
        auto result     = rpc.call("getchaintips")["result"];
        char next_label = 'b';
        for (const auto& t : result) {
            std::string status = t.value("status", "");
            std::string hash   = t.value("hash", "");
            if (status != "active" && known.find(hash) == known.end())
                continue;
            ChainTipInfo ti;
            ti.height = t.value("height", 0);
            ti.hash   = std::move(hash);
            ti.status = std::move(status);
            if (ti.status == "active") {
                ti.label = 'A';
            } else {
                ti.label = next_label++;
            }
            tips.push_back(std::move(ti));
        }
    } catch (...) {
    }
    return tips;
}

// Copy tracker state into the shared SlowBlocksState for rendering,
// computing tip labels by walking prev_map backwards from each tip.
void sync_state(const BlockTracker& tracker, const std::vector<ChainTipInfo>& tips,
                Guarded<SlowBlocksState>& sb_state) {
    const auto& blocks   = tracker.blocks();
    const auto& prev_map = tracker.prev_map();

    // Build hash→index lookup
    std::map<std::string, size_t> hash_to_idx;
    for (size_t i = 0; i < blocks.size(); ++i) {
        hash_to_idx[blocks[i].hash] = i;
    }

    // Compute tip membership strings
    size_t                   n_tips = tips.size();
    std::vector<std::string> tip_strings(blocks.size(), std::string(n_tips, ' '));
    for (size_t ti = 0; ti < tips.size(); ++ti) {
        std::string cur = tips[ti].hash;
        while (!cur.empty()) {
            auto it = hash_to_idx.find(cur);
            if (it != hash_to_idx.end()) {
                tip_strings[it->second][ti] = tips[ti].label;
            }
            auto pit = prev_map.find(cur);
            if (pit != prev_map.end()) {
                cur = pit->second;
            } else {
                break;
            }
        }
    }

    // Convert BlockInfo to BlockEvent
    std::vector<BlockEvent> events;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& bi = blocks[i];
        BlockEvent  ev;
        ev.height         = bi.height;
        ev.hash           = bi.hash;
        ev.time_header    = bi.time_header;
        ev.via_compact    = bi.via_compact;
        ev.time_block     = bi.time_block;
        ev.txns_requested = bi.txns_requested;
        ev.validation_ms  = bi.validation_ms;
        ev.tips           = tip_strings[i];
        ev.size_bytes     = bi.size_bytes;
        ev.tx_count       = bi.tx_count;
        events.push_back(std::move(ev));
    }

    std::sort(events.begin(), events.end(), [](const BlockEvent& a, const BlockEvent& b) {
        if (a.time_header != b.time_header)
            return a.time_header < b.time_header;
        return a.height < b.height;
    });

    sb_state.update([&](auto& s) {
        s.blocks = std::move(events);
        s.tips   = tips;
    });
}

// Enrich new blocks with RPC data, then sync display once.
// Result of enriching a single block via RPC
struct EnrichResult {
    std::string hash;
    std::string prev_hash;
    int         height     = 0;
    int         size_bytes = 0;
    int         tx_count   = 0;
    bool        ok         = false;
};

// Fetch header+block data for a hash. No tracker access needed.
EnrichResult enrich_one_rpc(const std::string& hash, RpcClient& rpc) {
    EnrichResult r;
    r.hash = hash;
    try {
        auto hdr    = rpc.call("getblockheader", {hash})["result"];
        r.prev_hash = hdr.value("previousblockhash", "");
        r.height    = hdr.value("height", 0);

        auto blk     = rpc.call("getblock", {hash, 1})["result"];
        r.size_bytes = blk.value("size", 0);
        r.tx_count   = blk.value("nTx", 0);
        r.ok         = true;
    } catch (...) {
    }
    return r;
}

// Apply enrichment result to the tracker.
void apply_enrich(BlockTracker& tracker, const EnrichResult& r) {
    if (!r.ok)
        return;
    tracker.set_prev(r.hash, r.prev_hash);
    for (auto& b : tracker.blocks()) {
        if (b.hash == r.hash) {
            b.prev_hash  = r.prev_hash;
            b.size_bytes = r.size_bytes;
            b.tx_count   = r.tx_count;
            break;
        }
    }
}

// Enrich new blocks, backfilling parents. Does RPC without holding
// the tracker lock. Takes hashes and a snapshot of known hashes,
// returns all results to apply.
struct EnrichBatch {
    std::vector<EnrichResult> results;
    std::vector<BlockInfo>    new_parents; // backfilled parent blocks
};

EnrichBatch enrich_rpc(std::vector<std::string> hashes, std::set<std::string> known_hashes,
                       std::map<std::string, TimePoint> hash_to_header_time, RpcClient& rpc) {
    EnrichBatch batch;

    for (const auto& hash : hashes) {
        auto r = enrich_one_rpc(hash, rpc);
        batch.results.push_back(r);
        if (!r.ok)
            continue;

        // Walk backwards through parents, creating missing blocks
        TimePoint child_time =
            hash_to_header_time.count(hash) ? hash_to_header_time[hash] : TimePoint{};
        std::string cur = r.prev_hash;
        for (int i = 0; i < 10 && !cur.empty(); ++i) {
            if (known_hashes.count(cur))
                break;

            auto pr = enrich_one_rpc(cur, rpc);
            if (!pr.ok)
                break;

            batch.results.push_back(pr);
            known_hashes.insert(cur);

            BlockInfo bi;
            bi.height      = pr.height;
            bi.hash        = cur;
            bi.time_header = child_time;
            bi.prev_hash   = pr.prev_hash;
            bi.size_bytes  = pr.size_bytes;
            bi.tx_count    = pr.tx_count;
            bi.was_tip     = true;
            batch.new_parents.push_back(std::move(bi));

            cur = pr.prev_hash;
        }
    }

    return batch;
}

// Apply a batch of enrichment results to the tracker.
void apply_batch(BlockTracker& tracker, const EnrichBatch& batch) {
    for (const auto& r : batch.results) {
        apply_enrich(tracker, r);
    }
    for (const auto& bi : batch.new_parents) {
        // Don't add duplicates
        bool found = false;
        for (const auto& b : tracker.blocks()) {
            if (b.hash == bi.hash) {
                found = true;
                break;
            }
        }
        if (!found) {
            tracker.blocks().push_back(bi);
        }
    }
    tracker.trim();
}

// Log thread: tails debug.log, updates tracker and sb_state. No RPC.
void log_thread_fn(const std::string& path, Guarded<SlowBlocksState>& sb_state,
                   Guarded<BlockTracker>& g_tracker, std::atomic<bool>& running,
                   const std::function<void()>& wake_ui) {
    constexpr int64_t TAIL_BYTES    = 100 * 1024 * 1024;
    constexpr size_t  MAX_LOG_LINES = 5;

    auto push_line = [&](const std::string& l) {
        sb_state.update([&](auto& s) {
            s.recent_log_lines.push_back(l);
            while (s.recent_log_lines.size() > MAX_LOG_LINES)
                s.recent_log_lines.pop_front();
        });
    };

    sb_state.update([](auto& s) { s.status = "opening..."; });

    std::ifstream f(path);
    if (!f) {
        sb_state.update([&](auto& s) { s.status = "error: cannot open " + path; });
        wake_ui();
        return;
    }

    f.seekg(0, std::ios::end);
    auto file_size = f.tellg();
    if (file_size > TAIL_BYTES) {
        f.seekg(file_size - TAIL_BYTES);
        std::string discard;
        std::getline(f, discard);
    } else {
        f.seekg(0);
    }

    sb_state.update([](auto& s) { s.status = "reading..."; });
    wake_ui();

    std::string line;
    int64_t     count = 0;

    // Phase 1: read existing content
    while (std::getline(f, line)) {
        if (!running)
            return;
        auto ev = parse_log_line(line);
        if (ev)
            g_tracker.update([&](auto& t) { t.process(*ev); });
        push_line(line);
        ++count;
        if ((count % 100000) == 0) {
            sb_state.update([&](auto& s) {
                s.lines_parsed = count;
                s.status       = "reading...";
            });
            wake_ui();
        }
    }

    // Initial sync (no tips — RPC thread will provide them)
    g_tracker.access([&](const auto& t) {
        std::vector<ChainTipInfo> no_tips;
        sync_state(t, no_tips, sb_state);
    });
    sb_state.update([&](auto& s) {
        s.lines_parsed = count;
        s.status       = "tailing";
    });
    wake_ui();

    // Phase 2: tail for new lines
    f.clear();
    while (running) {
        if (std::getline(f, line)) {
            auto ev = parse_log_line(line);
            if (ev) {
                if (ev->type == LogEvent::CONNECT_START) {
                    sb_state.update([](auto& s) { s.validating_since = Clock::now(); });
                } else if (ev->type == LogEvent::UPDATE_TIP ||
                           ev->type == LogEvent::CONNECT_BLOCK) {
                    sb_state.update([](auto& s) { s.validating_since = std::nullopt; });
                }

                g_tracker.update([&](auto& t) { t.process(*ev); });

                // Sync with existing tips from sb_state
                g_tracker.access([&](const auto& t) {
                    auto tips = sb_state.access([](const auto& s) { return s.tips; });
                    sync_state(t, tips, sb_state);
                });
                wake_ui();
            }
            push_line(line);
            sb_state.update([&](auto& s) { s.lines_parsed = ++count; });
        } else {
            f.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

// RPC thread: enriches blocks and fetches chain tips.
void rpc_thread_fn(Guarded<SlowBlocksState>& sb_state, Guarded<BlockTracker>& g_tracker,
                   std::atomic<bool>& running, const std::function<void()>& wake_ui, RpcConfig cfg,
                   Guarded<RpcAuth>& auth) {
    // Wait for log thread to finish initial read
    while (running) {
        auto status = sb_state.access([](const auto& s) { return s.status; });
        if (status == "tailing")
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto do_enrich = [&] {
        // Snapshot tracker state under lock
        std::vector<std::string>         hashes;
        std::set<std::string>            known;
        std::map<std::string, TimePoint> header_times;
        g_tracker.update([&](auto& t) {
            hashes = t.take_new_hashes();
            for (const auto& b : t.blocks()) {
                known.insert(b.hash);
                header_times[b.hash] = b.time_header;
            }
        });

        if (hashes.empty())
            return false;

        sb_state.update([](auto& s) { s.status = "enriching..."; });
        wake_ui();

        // Fresh RpcClient each batch (auth may change on node restart)
        RpcClient rpc(cfg, auth);

        // RPC calls — no lock held
        auto batch = enrich_rpc(std::move(hashes), std::move(known), std::move(header_times), rpc);

        // Apply results under lock
        g_tracker.update([&](auto& t) { apply_batch(t, batch); });

        // Snapshot known hashes, then fetch tips without lock
        std::set<std::string> tip_known;
        g_tracker.access([&](const auto& t) {
            for (const auto& b : t.blocks())
                tip_known.insert(b.hash);
        });
        auto tips = fetch_tips(rpc, tip_known);

        // Sync state under lock
        g_tracker.access([&](const auto& t) { sync_state(t, tips, sb_state); });
        sb_state.update([](auto& s) { s.status = "tailing"; });
        wake_ui();
        return true;
    };

    do_enrich();

    auto last_tips = Clock::now();
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!running)
            break;
        do_enrich();
        if (Clock::now() - last_tips > 5s) {
            std::set<std::string> tip_known;
            g_tracker.access([&](const auto& t) {
                for (const auto& b : t.blocks())
                    tip_known.insert(b.hash);
            });
            RpcClient rpc(cfg, auth);
            auto      tips = fetch_tips(rpc, tip_known);

            // Check logging categories
            std::string warn;
            try {
                auto                     logging = rpc.call("logging")["result"];
                std::vector<std::string> need    = {"cmpctblock", "bench"};
                std::vector<std::string> missing;
                for (const auto& cat : need) {
                    if (!logging.value(cat, false)) {
                        missing.push_back(cat);
                    }
                }
                if (!missing.empty()) {
                    std::string list, json_arr;
                    for (size_t i = 0; i < missing.size(); ++i) {
                        if (i > 0) {
                            list += ", ";
                            json_arr += ", ";
                        }
                        list += missing[i];
                        json_arr += "\"" + missing[i] + "\"";
                    }
                    warn = "Missing log categories: " + list + " (run: bitcoin-cli logging '[" +
                           json_arr + "]')";
                }
            } catch (...) {
            }

            g_tracker.access([&](const auto& t) { sync_state(t, tips, sb_state); });
            sb_state.update([&](auto& s) { s.warning = std::move(warn); });
            wake_ui();
            last_tips = Clock::now();
        }
    }
}

// Tick thread: wakes the UI once per second for the validation timer.
void tick_thread_fn(std::atomic<bool>& running, const std::function<void()>& wake_ui) {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (running)
            wake_ui();
    }
}

} // namespace

SlowBlocksTab::SlowBlocksTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                             std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
                             std::string debug_log_path)
    : Tab(std::move(cfg), auth, screen, running, state, refresh_secs),
      debug_log_path_(std::move(debug_log_path)) {
    auto      wake     = [&screen] { screen.PostEvent(ftxui::Event::Custom); };
    RpcConfig cfg_copy = cfg_;

    log_thread_ = std::thread(log_thread_fn, std::cref(debug_log_path_), std::ref(sb_state_),
                              std::ref(tracker_), std::ref(running_), wake);

    rpc_thread_ = std::thread(rpc_thread_fn, std::ref(sb_state_), std::ref(tracker_),
                              std::ref(running_), wake, std::move(cfg_copy), std::ref(auth_));

    tick_thread_ = std::thread(tick_thread_fn, std::ref(running_), wake);
}

Element SlowBlocksTab::key_hints(const AppState& snap) const {
    return hbox({refresh_indicator(snap),
                 text("  [Tab/\u2190/\u2192] switch  [q] quit ") | color(Color::GrayDark)});
}

Element SlowBlocksTab::render(const AppState& /*snap*/) {
    // Copy data out under the lock, then release
    std::vector<BlockEvent>   blocks;
    std::vector<ChainTipInfo> tips;
    std::string               log_status;
    int64_t                   lines_parsed = 0;
    std::vector<std::string>  log_lines;
    std::optional<TimePoint>  validating_since;
    std::string               warning;
    sb_state_.access([&](const auto& s) {
        blocks       = s.blocks;
        tips         = s.tips;
        log_status   = s.status;
        lines_parsed = s.lines_parsed;
        log_lines.assign(s.recent_log_lines.begin(), s.recent_log_lines.end());
        validating_since = s.validating_since;
        warning          = s.warning;
    });

    // Column widths
    constexpr int W_HEIGHT = 10;
    constexpr int W_HASH   = 25;
    constexpr int W_HDR    = 15;
    constexpr int W_BLK    = 12;
    constexpr int W_CMPCT  = 20;
    constexpr int W_VALID  = 10;
    constexpr int W_SIZE   = 10;
    constexpr int W_TXS    = 7;

    // Block table header
    auto header_row = hbox({
                          text(" Height") | size(WIDTH, EQUAL, W_HEIGHT),
                          text(" Hash") | size(WIDTH, EQUAL, W_HASH),
                          text(" Header") | size(WIDTH, EQUAL, W_HDR),
                          text(" Block") | size(WIDTH, EQUAL, W_BLK),
                          text(" Compact") | size(WIDTH, EQUAL, W_CMPCT),
                          text(" Validate") | size(WIDTH, EQUAL, W_VALID),
                          text(" Size") | size(WIDTH, EQUAL, W_SIZE),
                          text(" TXs") | size(WIDTH, EQUAL, W_TXS),
                      }) |
                      color(Color::Cyan) | bold;

    // Find active tip height for display filtering
    int active_tip_height = 0;
    for (const auto& t : tips) {
        if (t.status == "active") {
            active_tip_height = t.height;
            break;
        }
    }

    // Block rows
    Elements rows;
    rows.push_back(header_row);
    rows.push_back(separator());

    for (const auto& b : blocks) {
        if (active_tip_height > 0 && b.height > active_tip_height + 5)
            continue;
        bool  on_active = b.tips.find('A') != std::string::npos;
        Color row_color = Color::GrayDark;
        Color val_color = Color::GrayDark;
        if (on_active) {
            row_color = Color::Default;
            val_color = validation_color(b.validation_ms);
        }

        std::string height_str = std::to_string(b.height);

        std::string block_str;
        Color       blk_color = row_color;
        if (b.time_block) {
            double delta = to_seconds(*b.time_block - b.time_header);
            block_str    = fmt_delta(delta);
            if (on_active)
                blk_color = block_delay_color(delta);
        }
        // Validation column: show live timer if this is the block being validated
        std::string valid_str;
        if (b.validation_ms >= 0) {
            valid_str = fmt_validation_time(b.validation_ms);
        } else if (validating_since && &b == &blocks.back()) {
            double elapsed = to_seconds(Clock::now() - *validating_since);
            if (elapsed > 2.0) {
                valid_str = fmt_validation_time(elapsed * 1000.0) + "...";
                val_color = Color::Red;
            }
        }

        Elements row_cells = {
            text(" " + height_str) | size(WIDTH, EQUAL, W_HEIGHT - 3) | color(row_color),
            text(" " + b.tips) | size(WIDTH, EQUAL, 3) | color(Color::Cyan),
            text(" " + abbrev_hash(b.hash)) | size(WIDTH, EQUAL, W_HASH) | color(row_color),
            text(" " + fmt_time(b.time_header)) | size(WIDTH, EQUAL, W_HDR) | color(row_color),
            text(" " + block_str) | size(WIDTH, EQUAL, W_BLK) | color(blk_color),
            text(" " + fmt_compact(b)) | size(WIDTH, EQUAL, W_CMPCT) | color(row_color),
            text(" " + valid_str) | size(WIDTH, EQUAL, W_VALID) | color(val_color) | bold,
            text(" " + fmt_size(b.size_bytes)) | size(WIDTH, EQUAL, W_SIZE) | color(row_color),
            text(" " + std::to_string(b.tx_count)) | size(WIDTH, EQUAL, W_TXS) | color(row_color),
        };
        rows.push_back(hbox(row_cells));
    }

    auto block_table = section_box("Recent Blocks", rows);

    // Chain tips panel
    Elements tip_rows;
    for (const auto& t : tips) {
        Color tip_color = Color::GrayDark;
        if (t.status == "active")
            tip_color = Color::Green;
        else if (t.status == "valid-fork")
            tip_color = Color::Yellow;
        else if (t.status == "valid-headers")
            tip_color = Color::Yellow;
        std::string label_str(1, t.label);
        tip_rows.push_back(hbox({
            text("  " + label_str) | color(Color::Cyan),
            text("  " + t.status) | size(WIDTH, EQUAL, 16) | color(tip_color),
            text("  height=" + std::to_string(t.height)),
            text("  " + t.hash) | color(Color::GrayDark),
        }));
    }

    auto tips_panel = section_box("Recent Chain Tips", tip_rows);

    // Log watcher status + recent lines
    Elements log_rows;
    log_rows.push_back(hbox({
        text(" Log Watcher ") | bold | color(Color::Gold1),
        text(" " + debug_log_path_) | color(Color::GrayDark),
        text("  [" + log_status + ", " + std::to_string(lines_parsed) + " lines]") |
            color(Color::GrayDark),
    }));
    for (const auto& l : log_lines) {
        log_rows.push_back(text("  " + l));
    }
    auto log_panel = vbox(log_rows) | border;

    Elements panels;
    if (!warning.empty()) {
        panels.push_back(text(" " + warning) | bold | color(Color::Red) | border);
    }
    panels.push_back(block_table);
    panels.push_back(tips_panel);
    panels.push_back(log_panel);
    return vbox(panels) | flex;
}

void SlowBlocksTab::join() {
    if (log_thread_.joinable())
        log_thread_.join();
    if (rpc_thread_.joinable())
        rpc_thread_.join();
    if (tick_thread_.joinable())
        tick_thread_.join();
}
