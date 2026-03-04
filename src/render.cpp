#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "format.hpp"
#include "render.hpp"

using namespace ftxui;

// ============================================================================
// Internal helpers
// ============================================================================
static Element section_box(const std::string& title, Elements rows) {
    Elements content;
    content.reserve(rows.size() + 1);
    content.push_back(text(" " + title + " ") | bold | color(Color::Gold1));
    for (auto& r : rows)
        content.push_back(std::move(r));
    return vbox(std::move(content)) | border;
}

// ============================================================================
// Exported rendering functions
// ============================================================================
Element label_value(const std::string& lbl, const std::string& val, Color val_color) {
    return hbox({
        text(lbl) | color(Color::GrayDark),
        text(val) | color(val_color) | bold,
    });
}

// --- Dashboard --------------------------------------------------------------
Element render_dashboard(const AppState& s) {
    // Chain section
    std::string chain_color_name = s.chain == "main" ? "mainnet" : s.chain;
    Color       chain_color      = (s.chain == "main") ? Color::Green : Color::Yellow;

    auto blockchain_section = section_box(
        "Blockchain",
        {
            label_value("  Chain       : ", chain_color_name, chain_color),
            label_value("  Height      : ", fmt_height(s.blocks)),
            label_value("  Headers     : ", fmt_height(s.headers)),
            label_value("  Difficulty  : ", fmt_difficulty(s.difficulty)),
            label_value("  Hash Rate   : ", fmt_hashrate(s.network_hashps)),
            hbox({
                text("  Sync        : ") | color(Color::GrayDark),
                gauge(static_cast<float>(s.progress)) | flex |
                    color(s.progress >= 1.0 ? Color::Green : Color::Yellow),
                text(" " + std::to_string(static_cast<int>(s.progress * 100)) + "%") | bold,
            }),
            label_value("  IBD         : ", s.ibd ? "yes" : "no",
                        s.ibd ? Color::Yellow : Color::Green),
            label_value("  Pruned      : ", s.pruned ? "yes" : "no"),
        });

    // Network section
    Color net_color       = s.network_active ? Color::Green : Color::Red;
    auto  network_section = section_box(
        "Network", {
                       label_value("  Active      : ", s.network_active ? "yes" : "no", net_color),
                       label_value("  Connections : ", std::to_string(s.connections)),
                       label_value("    In        : ", std::to_string(s.connections_in)),
                       label_value("    Out       : ", std::to_string(s.connections_out)),
                       label_value("  Client      : ", s.subversion),
                       label_value("  Protocol    : ", std::to_string(s.protocol_version)),
                       label_value("  Relay fee   : ", fmt_satsvb(s.relay_fee)),
                   });

    // Mempool section
    double usage_frac      = s.mempool_max > 0 ? static_cast<double>(s.mempool_usage) /
                                                static_cast<double>(s.mempool_max)
                                               : 0.0;
    auto   mempool_section = section_box(
        "Mempool",
        {
            label_value("  Transactions: ", fmt_int(s.mempool_tx)),
            label_value("  Size        : ", fmt_bytes(s.mempool_bytes)),
            label_value("  Total fee   : ", fmt_btc(s.total_fee, 4)),
            label_value("  Min fee     : ", fmt_satsvb(s.mempool_min_fee)),
            hbox({
                text("  Memory      : ") | color(Color::GrayDark),
                gauge(static_cast<float>(usage_frac)) | flex |
                    color(usage_frac > 0.8 ? Color::Red : Color::Cyan),
                text(" " + fmt_bytes(s.mempool_usage) + " / " + fmt_bytes(s.mempool_max)) | bold,
            }),
        });

    return vbox({
               hbox({
                   blockchain_section | flex,
                   network_section | flex,
               }),
               mempool_section,
           }) |
           flex;
}

// --- Mempool ----------------------------------------------------------------
Element render_mempool(const AppState& s) {
    double usage_frac  = s.mempool_max > 0 ? static_cast<double>(s.mempool_usage) /
                                                static_cast<double>(s.mempool_max)
                                           : 0.0;
    Color  usage_color = usage_frac > 0.8   ? Color::Red
                         : usage_frac > 0.5 ? Color::Yellow
                                            : Color::Cyan;

    auto stats_section = section_box(
        "Mempool", {
                       label_value("  Transactions    : ", fmt_int(s.mempool_tx)),
                       label_value("  Virtual size    : ", fmt_bytes(s.mempool_bytes)),
                       label_value("  Total fees      : ", fmt_btc(s.total_fee)),
                       label_value("  Min relay fee   : ", fmt_satsvb(s.mempool_min_fee)),
                       separator(),
                       text("  Memory usage") | color(Color::GrayDark),
                       hbox({
                           text("  "),
                           gauge(static_cast<float>(usage_frac)) | flex | color(usage_color),
                           text("  "),
                       }),
                       hbox({
                           text("  Used : ") | color(Color::GrayDark),
                           text(fmt_bytes(s.mempool_usage)) | bold,
                           text("  /  Max : ") | color(Color::GrayDark),
                           text(fmt_bytes(s.mempool_max)) | bold,
                       }),
                   });

    // Block visualization — vertical fill bars, one column per block.
    Element blocks_section;
    if (s.recent_blocks.empty()) {
        blocks_section =
            section_box("Recent Blocks", {text("  Fetching…") | color(Color::GrayDark)});
    } else {
        const int     BAR_HEIGHT = 6;
        const int     COL_WIDTH  = 10;
        const int64_t MAX_WEIGHT = 4'000'000LL;

        // Determine animation phase.
        bool anim_slide = s.block_anim_active && !s.block_anim_old.empty();

        // During slide: render old blocks minus the last (it slides off the right edge).
        const std::vector<BlockStat>& src        = anim_slide ? s.block_anim_old : s.recent_blocks;
        int                           num        = static_cast<int>(src.size());
        int                           max_render = anim_slide ? std::max(0, num - 1) : num;

        // Slide offset grows from 0 → (COL_WIDTH+1) chars over SLIDE_FRAMES frames.
        int left_pad = 0;
        if (anim_slide) {
            double progress = (s.block_anim_frame + 1.0) / BLOCK_ANIM_SLIDE_FRAMES;
            left_pad        = static_cast<int>(std::round(progress * (COL_WIDTH + 1)));
        }

        Elements block_cols;
        for (int i = 0; i < max_render; ++i) {
            const auto& b = src[i];
            double fill   = b.total_weight > 0 ? std::min(1.0, static_cast<double>(b.total_weight) /
                                                                   static_cast<double>(MAX_WEIGHT))
                                               : 0.0;

            Color bar_color = fill > 0.9   ? Color(Color::DarkOrange)
                              : fill > 0.7 ? Color(Color::Yellow)
                                           : Color(Color::Green);

            int filled_rows = static_cast<int>(std::round(fill * BAR_HEIGHT));

            Elements bar;
            for (int r = 0; r < BAR_HEIGHT; ++r) {
                bool is_filled = r >= (BAR_HEIGHT - filled_rows);
                bar.push_back(is_filled ? text("██████████") | color(bar_color)
                                        : text("░░░░░░░░░░") | color(Color::GrayDark));
            }

            if (!block_cols.empty())
                block_cols.push_back(text(" "));

            block_cols.push_back(
                vbox({
                    vbox(std::move(bar)),
                    text(fmt_height(b.height)) | center,
                    text(fmt_int(b.txs) + " tx") | center | color(Color::GrayDark),
                    text(fmt_bytes(b.total_size)) | center | color(Color::GrayDark),
                }) |
                size(WIDTH, EQUAL, COL_WIDTH));
        }

        // Compose row: optional slide-offset pad on the left, then blocks.
        Element blocks_row =
            left_pad > 0 ? hbox({text(std::string(left_pad, ' ')), hbox(std::move(block_cols))})
                         : hbox(std::move(block_cols));

        blocks_section =
            section_box("Recent Blocks", {text(""), hbox({text("  "), std::move(blocks_row)})});
    }

    return vbox({
        stats_section,
        blocks_section,
    });
}

// --- Network ----------------------------------------------------------------
Element render_network(const AppState& s) {
    return vbox({
               section_box(
                   "Network Status",
                   {
                       label_value("  Network active : ", s.network_active ? "yes" : "no",
                                   s.network_active ? Color::Green : Color::Red),
                       label_value("  Total peers    : ", std::to_string(s.connections)),
                       label_value("  Inbound        : ", std::to_string(s.connections_in)),
                       label_value("  Outbound       : ", std::to_string(s.connections_out)),
                   }),
               section_box(
                   "Node",
                   {
                       label_value("  Client version : ", s.subversion),
                       label_value("  Protocol       : ", std::to_string(s.protocol_version)),
                       label_value("  Relay fee      : ", fmt_satsvb(s.relay_fee)),
                   }),
               filler(),
           }) |
           flex;
}

// --- Peers ------------------------------------------------------------------
Element render_peers(const AppState& s) {
    if (s.peers.empty()) {
        return vbox({
                   text("No peers connected.") | color(Color::GrayDark) | center,
                   filler(),
               }) |
               flex;
    }

    // Right-align text inside a fixed-width cell.
    auto rcell = [](Element e, int w) -> Element {
        return hbox({filler(), std::move(e)}) | size(WIDTH, EQUAL, w);
    };

    // Header row
    Elements rows;
    rows.push_back(hbox({
                       text("ID") | bold | size(WIDTH, EQUAL, 5),
                       text("Address") | bold | flex,
                       text("Net") | bold | size(WIDTH, EQUAL, 5),
                       text("I/O") | bold | size(WIDTH, EQUAL, 4),
                       rcell(text("Ping ms") | bold, 8),
                       rcell(text("Recv") | bold, 10),
                       rcell(text("Sent") | bold, 10),
                       rcell(text("Height") | bold, 9),
                   }) |
                   color(Color::Gold1));
    rows.push_back(separator());

    for (const auto& p : s.peers) {
        std::string ping_str = p.ping_ms >= 0.0 ? [&] {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << p.ping_ms;
            return ss.str();
        }()
                                                : "—";

        Color       io_color = p.inbound ? Color::Cyan : Color::Green;
        std::string net_str  = p.network.empty() ? "?" : p.network.substr(0, 4);

        rows.push_back(hbox({
            text(std::to_string(p.id)) | size(WIDTH, EQUAL, 5),
            text(p.addr) | flex,
            text(net_str) | size(WIDTH, EQUAL, 5),
            text(p.inbound ? "in" : "out") | color(io_color) | size(WIDTH, EQUAL, 4),
            rcell(text(ping_str), 8),
            rcell(text(fmt_bytes(p.bytes_recv)), 10),
            rcell(text(fmt_bytes(p.bytes_sent)), 10),
            rcell(text(fmt_height(p.synced_blocks)), 9),
        }));
    }

    return vbox({
               vbox(std::move(rows)) | border | flex,
           }) |
           flex;
}

// --- Tools ------------------------------------------------------------------
Element render_tools(const AppState& snap, const BroadcastState& bs, bool input_active,
                     const std::string& hex_str, int tools_sel) {
    // sel==1 is the result txid row (when success).
    bool has_result_row = bs.has_result && bs.success;

    // ── Broadcast section ────────────────────────────────────────────────────
    auto menu_row = [](const std::string& label, const std::string& shortcut,
                       bool selected) -> Element {
        auto row =
            hbox({text("  " + label) | flex, text(shortcut + "  ") | color(Color::GrayDark)});
        return selected ? row | inverted : row;
    };

    Elements bcast_rows;
    bcast_rows.push_back(text(""));
    bcast_rows.push_back(menu_row("Broadcast", "[b]", tools_sel == 0 && !input_active));

    if (!input_active && !bs.submitting) {
        bcast_rows.push_back(text(""));
        bcast_rows.push_back(
            text("  Submits to your node, which relays the transaction to all connected") |
            color(Color::GrayDark));
        bcast_rows.push_back(text("  peers over the P2P network.") | color(Color::GrayDark));
    }

    if (input_active) {
        bcast_rows.push_back(separator());
        // Wrap hex across rows of 70 chars; all rows shown.
        constexpr int kHexCols = 70;
        const auto&   h        = hex_str;
        int           total    = (int)h.size();
        std::vector<std::string> chunks;
        for (int off = 0; off < std::max(total, 1); off += kHexCols)
            chunks.push_back(h.substr(off, std::min(kHexCols, total - off)));
        if (chunks.empty()) chunks.push_back("");
        for (int i = 0; i < (int)chunks.size(); ++i) {
            bool is_last = i == (int)chunks.size() - 1;
            auto prefix  = i == 0 && chunks.size() == 1
                               ? text("  Hex  : ") | color(Color::GrayDark)
                               : (i == 0 ? text("  Hex  : ") | color(Color::GrayDark)
                                         : text("         ") | color(Color::GrayDark));
            bcast_rows.push_back(hbox({
                prefix,
                text(chunks[i]) | color(Color::White),
                is_last ? text("│") | color(Color::White) : text(""),
                filler(),
            }));
        }
        bcast_rows.push_back(text("  [Enter] submit  [Esc] cancel") | color(Color::GrayDark));
    } else if (bs.submitting) {
        bcast_rows.push_back(separator());
        bcast_rows.push_back(text("  Broadcasting…") | color(Color::Yellow));
    } else if (bs.has_result) {
        bcast_rows.push_back(separator());
        if (bs.success) {
            bool txid_sel = (tools_sel == 1);
            auto txid_row = hbox({
                text("  ✓ ") | color(Color::Green) | bold,
                text(bs.result_txid) | color(Color::White),
                filler(),
            });
            bcast_rows.push_back(txid_sel ? txid_row | inverted : txid_row);
        } else {
            bcast_rows.push_back(text("  Error: ") | color(Color::Red) | bold);
            // Word-wrap error at ~72 chars with consistent indent (newlines treated as spaces).
            constexpr int      kErrCols = 72;
            constexpr auto     kIndent  = "  ";
            std::istringstream ss(bs.result_error);
            std::string        word, cur = kIndent;
            while (ss >> word) {
                if (cur.size() > strlen(kIndent) &&
                    (int)(cur.size() + 1 + word.size()) > kErrCols) {
                    bcast_rows.push_back(text(cur) | color(Color::White));
                    cur = kIndent;
                }
                if (cur.size() > strlen(kIndent))
                    cur += ' ';
                cur += word;
            }
            if (cur.size() > strlen(kIndent))
                bcast_rows.push_back(text(cur) | color(Color::White));
        }
    }

    auto bcast_section = section_box("Broadcast Transaction", bcast_rows);

    // ── Private broadcast queue (only shown when non-empty) ─────────────────
    Elements layout;
    layout.push_back(bcast_section);

    if (!snap.privbcast_txids.empty()) {
        Elements queue_rows;
        for (int i = 0; i < (int)snap.privbcast_txids.size(); ++i) {
            queue_rows.push_back(hbox({
                text("  ") | color(Color::GrayDark),
                text(snap.privbcast_txids[i]) | color(Color::White),
                filler(),
            }));
        }
        layout.push_back(section_box("Private Broadcast Queue", queue_rows));
    }

    layout.push_back(filler());
    return vbox(std::move(layout)) | flex;
}
