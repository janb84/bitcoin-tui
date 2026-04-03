#include "slowblocks.hpp"

#include <chrono>
#include <fstream>
#include <optional>
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

struct BlockEntry {
    int                      height;
    std::string              hash;
    TimePoint                time_header;    // when we first saw the header
    std::optional<TimePoint> time_block;     // when we got the full block
    bool                     compact;
    int                      txns_requested;
    double                   validation_ms;  // -1 if not yet validated
    int                      size_bytes;
    int                      tx_count;
    std::string              tips;           // which chain tips this block is an ancestor of (e.g. "Ab")
};

struct ChainTip {
    char        label;  // 'A' for active, 'b','c','d'... for forks
    int         height;
    std::string hash;
    std::string status; // "active", "valid-fork", "valid-headers", "headers-only", "invalid"
};

double to_seconds(Clock::duration d) {
    return std::chrono::duration<double>(d).count();
}

Color block_delay_color(double seconds) {
    if (seconds < 1)    return Color::Green;
    if (seconds < 10)   return Color::Yellow;
    if (seconds < 60)   return Color(uint8_t(255), uint8_t(165), uint8_t(0)); // orange
    return Color::Red;
}

Color validation_color(double ms) {
    if (ms < 100)    return Color::Green;
    if (ms < 1000)   return Color::Yellow;
    if (ms < 10000)  return Color(uint8_t(255), uint8_t(165), uint8_t(0)); // orange
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
    auto tt = Clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1'000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03ld", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long>(ms.count()));
    return buf;
}

std::string fmt_delta(double seconds) {
    char buf[32];
    if (seconds < 60) {
        snprintf(buf, sizeof(buf), "+%.3fs", seconds);
    } else {
        int mins = static_cast<int>(seconds) / 60;
        double secs = seconds - mins * 60;
        snprintf(buf, sizeof(buf), "+%dm %.0fs", mins, secs);
    }
    return buf;
}

std::string fmt_compact(const BlockEntry& b) {
    if (!b.time_block) return "";
    if (!b.compact) return "no";
    if (b.txns_requested > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "yes (%d requested)", b.txns_requested);
        return buf;
    }
    return "yes";
}

std::string abbrev_hash(const std::string& hash, size_t prefix = 12, size_t suffix = 8) {
    if (hash.size() <= prefix + suffix + 3) return hash;
    return hash.substr(0, prefix) + "..." + hash.substr(hash.size() - suffix);
}

// Helper: build a TimePoint from h:m:s.us on a fixed fake date
TimePoint make_time(int h, int m, int s, int us = 0) {
    // Use a fixed date: 2026-04-10
    std::tm tm{};
    tm.tm_year = 126; // 2026
    tm.tm_mon  = 3;   // April
    tm.tm_mday = 10;
    tm.tm_hour = h;
    tm.tm_min  = m;
    tm.tm_sec  = s;
    auto tp = Clock::from_time_t(timegm(&tm));
    return tp + std::chrono::microseconds(us);
}

// Fake data telling the slow-block reorg story:
//   420000-420004: normal blocks on both chains (tips "Ab")
//   420005-420008: slow chain only (tips "b")
//   420005'-420008': reorg chain, headers seen early, blocks fetched late (tips "A")
//   420009': only on active chain, triggers the reorg (tips "A")
std::vector<BlockEntry> fake_blocks() {
    return {
        //                                                                                    header time                 block time                  cmpct  req   valid     size     txs  tips
        // Common ancestors
        {420000, "00000000000000000125cf0e2cd3a8b9f4e6d7c1a2b3e4f5a6b7c8d9e0f1a2b3", make_time(14,20, 1,123456), make_time(14,20, 1,234567), true,  0,    18.3,  998213, 1843, "Ab"},
        {420001, "00000000000000000a3f718bc4e5d6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3", make_time(14,30,15,654321), make_time(14,30,15,789012), true,  0,    22.1, 1123456, 2106, "Ab"},
        {420002, "000000000000000003c82a649e1f2d3c4b5a6e7f8d9c0b1a2e3f4d5c6b7a8e9f", make_time(14,40, 8,111222), make_time(14,40, 8,333444), true,  3,    19.7, 1045678, 1957, "Ab"},
        {420003, "00000000000000000f19d3a85b2c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c", make_time(14,50,22,555666), make_time(14,50,22,777888), true,  0,    20.5, 1087234, 2002, "Ab"},
        {420004, "0000000000000000071b3e5c8a9d0e1f2c3b4a5d6e7f8c9b0a1d2e3f4c5b6a7d", make_time(15, 0,30,100200), make_time(15, 0,30,300400), true,  0,    17.8,  956789, 1790, "Ab"},
        // Slow blocks (fork b only)
        {420005, "00000000000000000e4a27f1d63b5c8d9e0f1a2b3c4d5e6f7a8b9c0d1e2f3a4b", make_time(15,10,45,500000), make_time(15,10,45,612345), true, 12,  5230.0,  876543, 1205, " b"},
        {420006, "00000000000000000b8c93e2a71d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d", make_time(15,21, 2,300000), make_time(15,21, 2,423456), true,  8, 32100.0,  745123,  988, " b"},
        {420007, "0000000000000000042d1fa7b58e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e", make_time(15,31,18,700000), make_time(15,31,18,812345), true,  5, 91200.0,  812345, 1103, " b"},
        {420008, "00000000000000000c5e84d3f92a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a", make_time(15,41,35,200000), make_time(15,41,35,345678), true,  2, 47800.0,  923456, 1544, " b"},
        // Reorg chain (active A): headers arrived earlier, blocks fetched when 420009 tips the balance
        {420005, "00000000000000000d7f36a2e84c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c", make_time(15,43, 0,100000), make_time(15,55, 1,200000), false, 0,    21.2, 1198765, 2235, "A "},
        {420006, "00000000000000000a1b82c4f96d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d", make_time(15,45, 0,200000), make_time(15,55, 2,300000), false, 0,    19.8, 1134567, 2088, "A "},
        {420007, "00000000000000000f3d95b7a12e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e", make_time(15,49, 0,300000), make_time(15,55, 3,400000), false, 0,    18.5, 1056789, 1946, "A "},
        {420008, "00000000000000000e6c41d8b35f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f", make_time(15,52, 0,400000), make_time(15,55, 4,500000), false, 0,    23.4, 1178901, 2157, "A "},
        {420009, "00000000000000000b2a73c5d81e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e", make_time(15,55, 0,500000), make_time(15,55, 0,612345), true,  0,    20.1, 1201234, 2301, "A "},
    };
}

std::vector<ChainTip> fake_tips() {
    return {
        {'A', 420009, "00000000000000000b2a73c5d81e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e", "active"},
        {'b', 420008, "00000000000000000c5e84d3f92a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a", "valid-fork"},
    };
}

void watch_log(const std::string& path,
               Guarded<SlowBlocksState>& sb_state,
               std::atomic<bool>& running,
               const std::function<void()>& wake_ui)
{
    constexpr int64_t TAIL_BYTES = 100 * 1024 * 1024; // 100 MB
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
        sb_state.update([&](auto& s) {
            s.status = "error: cannot open " + path;
        });
        wake_ui();
        return;
    }

    // Seek to near the end
    f.seekg(0, std::ios::end);
    auto file_size = f.tellg();
    if (file_size > TAIL_BYTES) {
        f.seekg(file_size - TAIL_BYTES);
        // Skip partial first line
        std::string discard;
        std::getline(f, discard);
    } else {
        f.seekg(0);
    }

    sb_state.update([](auto& s) { s.status = "reading..."; });
    wake_ui();

    BlockTracker tracker;
    std::string line;
    int64_t count = 0;

    // Phase 1: read existing content
    while (std::getline(f, line)) {
        if (!running) return;
        auto ev = parse_log_line(line);
        if (ev) tracker.process(*ev);
        push_line(line);
        ++count;
        if ((count % 100000) == 0) {
            sb_state.update([&](auto& s) {
                s.lines_parsed = count;
                s.status = "reading...";
            });
            wake_ui();
        }
    }

    // Commit initial state
    sb_state.update([&](auto& s) {
        s.lines_parsed = count;
        s.status = "tailing";
    });
    wake_ui();

    // Phase 2: tail for new lines
    f.clear();
    while (running) {
        if (std::getline(f, line)) {
            auto ev = parse_log_line(line);
            if (ev) tracker.process(*ev);
            push_line(line);
            sb_state.update([&](auto& s) {
                s.lines_parsed = ++count;
            });
            wake_ui();
        } else {
            f.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

} // namespace

SlowBlocksTab::SlowBlocksTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                             std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
                             std::string debug_log_path)
    : Tab(std::move(cfg), auth, screen, running, state, refresh_secs),
      debug_log_path_(std::move(debug_log_path))
{
    watcher_thread_ = std::thread(watch_log,
        std::cref(debug_log_path_), std::ref(sb_state_), std::ref(running_),
        [&screen] { screen.PostEvent(ftxui::Event::Custom); });
}

Element SlowBlocksTab::key_hints(const AppState& snap) const {
    return hbox({refresh_indicator(snap),
                 text("  [Tab/\u2190/\u2192] switch  [q] quit ") | color(Color::GrayDark)});
}

Element SlowBlocksTab::render(const AppState& /*snap*/) {
    auto sb = sb_state_.get();
    auto blocks = fake_blocks();
    auto tips   = fake_tips();

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
        text(" Height")     | size(WIDTH, EQUAL, W_HEIGHT),
        text(" Hash")       | size(WIDTH, EQUAL, W_HASH),
        text(" Header")     | size(WIDTH, EQUAL, W_HDR),
        text(" Block")      | size(WIDTH, EQUAL, W_BLK),
        text(" Compact")    | size(WIDTH, EQUAL, W_CMPCT),
        text(" Validate")   | size(WIDTH, EQUAL, W_VALID),
        text(" Size")       | size(WIDTH, EQUAL, W_SIZE),
        text(" TXs")        | size(WIDTH, EQUAL, W_TXS),
    }) | color(Color::Cyan) | bold;

    // Block rows
    Elements rows;
    rows.push_back(header_row);
    rows.push_back(separator());

    for (const auto& b : blocks) {
        bool on_active = b.tips.find('A') != std::string::npos;
        Color row_color = Color::GrayDark;
        Color val_color = Color::GrayDark;
        if (on_active) {
            row_color = Color::Default;
            val_color = validation_color(b.validation_ms);
        }

        std::string height_str = std::to_string(b.height);

        std::string block_str;
        Color blk_color = row_color;
        if (b.time_block) {
            double delta = to_seconds(*b.time_block - b.time_header);
            block_str = fmt_delta(delta);
            if (on_active) blk_color = block_delay_color(delta);
        }
        std::string valid_str = b.validation_ms < 0 ? "" : fmt_validation_time(b.validation_ms);

        Elements row_cells = {
            text(" " + height_str) | size(WIDTH, EQUAL, W_HEIGHT - 3) | color(row_color),
            text(" " + b.tips) | size(WIDTH, EQUAL, 3) | color(Color::Cyan),
            text(" " + abbrev_hash(b.hash))        | size(WIDTH, EQUAL, W_HASH)   | color(row_color),
            text(" " + fmt_time(b.time_header))    | size(WIDTH, EQUAL, W_HDR)    | color(row_color),
            text(" " + block_str)                  | size(WIDTH, EQUAL, W_BLK)    | color(blk_color),
            text(" " + fmt_compact(b))             | size(WIDTH, EQUAL, W_CMPCT)  | color(row_color),
            text(" " + valid_str)                  | size(WIDTH, EQUAL, W_VALID)  | color(val_color) | bold,
            text(" " + fmt_size(b.size_bytes))     | size(WIDTH, EQUAL, W_SIZE)   | color(row_color),
            text(" " + std::to_string(b.tx_count)) | size(WIDTH, EQUAL, W_TXS)   | color(row_color),
        };
        rows.push_back(hbox(row_cells));
    }

    auto block_table = section_box("Recent Blocks", rows);

    // Chain tips panel
    Elements tip_rows;
    for (const auto& t : tips) {
        Color tip_color = Color::GrayDark;
        if (t.status == "active") tip_color = Color::Green;
        else if (t.status == "valid-fork") tip_color = Color::Yellow;
        else if (t.status == "valid-headers") tip_color = Color::Yellow;
        std::string label_str(1, t.label);
        tip_rows.push_back(hbox({
            text("  " + label_str) | color(Color::Cyan),
            text("  " + t.status) | size(WIDTH, EQUAL, 16) | color(tip_color),
            text("  height=" + std::to_string(t.height)),
            text("  " + t.hash) | color(Color::GrayDark),
        }));
    }

    auto tips_panel = section_box("Chain Tips", tip_rows);

    // Log watcher status + recent lines
    Elements log_rows;
    log_rows.push_back(hbox({
        text(" Log Watcher ") | bold | color(Color::Gold1),
        text(" " + debug_log_path_) | color(Color::GrayDark),
        text("  [" + sb.status + ", " + std::to_string(sb.lines_parsed) + " lines]") | color(Color::GrayDark),
    }));
    for (const auto& l : sb.recent_log_lines) {
        log_rows.push_back(text("  " + l));
    }
    auto log_panel = vbox(log_rows) | border;

    return vbox({
               block_table,
               tips_panel,
               log_panel,
           }) |
           flex;
}

void SlowBlocksTab::join() {
    if (watcher_thread_.joinable()) watcher_thread_.join();
}
