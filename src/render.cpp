#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

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
Element render_mempool(const AppState& s, int mempool_sel) {
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
        const std::vector<BlockStat>& src = anim_slide ? s.block_anim_old : s.recent_blocks;
        int                           num = static_cast<int>(src.size());
        int max_cols   = std::max(1, (Terminal::Size().dimx - 4) / (COL_WIDTH + 1));
        int max_render = std::min(anim_slide ? std::max(0, num - 1) : num, max_cols);

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

            bool is_selected = (i == mempool_sel);
            block_cols.push_back(
                vbox({
                    vbox(std::move(bar)),
                    is_selected ? text(fmt_height(b.height)) | center | inverted | bold
                                : text(fmt_height(b.height)) | center,
                    text(fmt_int(b.txs) + " tx") | center | color(Color::GrayDark),
                    text(fmt_bytes(b.total_size)) | center | color(Color::GrayDark),
                    text(b.time > 0 ? fmt_time_ago(b.time) : "") | center | color(Color::GrayDark),
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
Element render_peers(const AppState& s, int selected) {
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

    for (int idx = 0; idx < static_cast<int>(s.peers.size()); ++idx) {
        const auto& p        = s.peers[idx];
        std::string ping_str = p.ping_ms >= 0.0 ? [&] {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << p.ping_ms;
            return ss.str();
        }()
                                                : "—";

        Color       io_color = p.inbound ? Color::Cyan : Color::Green;
        std::string net_str  = p.network.empty() ? "?" : p.network.substr(0, 4);

        auto row = hbox({
            text(std::to_string(p.id)) | size(WIDTH, EQUAL, 5),
            text(p.addr) | flex,
            text(net_str) | size(WIDTH, EQUAL, 5),
            text(p.inbound ? "in" : "out") | color(io_color) | size(WIDTH, EQUAL, 4),
            rcell(text(ping_str), 8),
            rcell(text(fmt_bytes(p.bytes_recv)), 10),
            rcell(text(fmt_bytes(p.bytes_sent)), 10),
            rcell(text(fmt_height(p.synced_blocks)), 9),
        });
        if (idx == selected)
            row = std::move(row) | inverted | focus;
        rows.push_back(std::move(row));
    }

    return vbox({
               vbox(std::move(rows)) | yframe | border | flex,
           }) |
           flex;
}

Element render_peer_detail(const PeerInfo& p, const PeerActionResult& action, int sel) {
    auto    now_secs = static_cast<int64_t>(std::time(nullptr));
    int64_t uptime   = p.conntime > 0 ? now_secs - p.conntime : 0;

    std::string ping_str = p.ping_ms >= 0.0 ? [&] {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << p.ping_ms;
        return ss.str() + " ms";
    }()
                                            : "—";

    std::string min_ping_str = p.min_ping_ms >= 0.0 ? [&] {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << p.min_ping_ms;
        return ss.str() + " ms";
    }()
                                                    : "—";

    std::string hb;
    if (p.bip152_hb_from && p.bip152_hb_to)
        hb = "both";
    else if (p.bip152_hb_from)
        hb = "from";
    else if (p.bip152_hb_to)
        hb = "to";
    else
        hb = "no";

    // IPv6 starts with '['; IPv4 host is all digits+dots before the port colon
    const bool ipv4_or_ipv6 = !p.addr.empty() && (p.addr[0] == '[' || [&] {
        auto colon = p.addr.find(':');
        auto host  = p.addr.substr(0, colon != std::string::npos ? colon : p.addr.size());
        return !host.empty() && std::all_of(host.begin(), host.end(), [](char c) {
            return std::isdigit((unsigned char)c) || c == '.';
        });
    }());

    Element addr_row = ipv4_or_ipv6 ? label_value("  Address     : ", p.addr)
                                    : vbox({text("  Address     : ") | color(Color::GrayDark),
                                            text("    " + p.addr) | bold});

    Elements detail_rows = {
        std::move(addr_row),
        label_value("  Direction   : ", p.inbound ? "inbound" : "outbound",
                    p.inbound ? Color::Cyan : Color::Green),
        label_value("  Network     : ", p.network.empty() ? "?" : p.network),
        label_value("  User agent  : ", p.subver),
        label_value("  Version     : ", std::to_string(p.version)),
        label_value("  Services    : ", p.services.empty() ? "—" : p.services),
        separator(),
        label_value("  Ping        : ", ping_str),
        label_value("  Min ping    : ", min_ping_str),
        label_value("  Connected   : ", uptime > 0 ? fmt_age(uptime) : "—"),
        label_value("  Conn type   : ", p.connection_type.empty() ? "—" : p.connection_type),
        label_value("  Transport   : ", p.transport.empty() ? "—" : p.transport),
        separator(),
        label_value("  Recv        : ", fmt_bytes(p.bytes_recv)),
        label_value("  Sent        : ", fmt_bytes(p.bytes_sent)),
        label_value("  Height      : ", fmt_height(p.synced_blocks)),
        label_value("  Start height: ", fmt_height(p.startingheight)),
        label_value("  HB compact  : ", hb),
        label_value("  Addrs proc  : ", fmt_int(p.addr_processed)),
    };

    detail_rows.push_back(separator());
    {
        auto btn = [](const std::string& label, bool selected) -> Element {
            auto e = text("  " + label + "  ");
            return selected ? (std::move(e) | inverted) : std::move(e);
        };
        detail_rows.push_back(hbox({
            btn("Disconnect", sel == 0),
            text("  "),
            btn("Ban (24h)", sel == 1),
            filler(),
        }));
    }
    if (action.has_result) {
        if (action.success)
            detail_rows.push_back(text("  \u2713 " + action.message) | color(Color::Green) | bold);
        else
            detail_rows.push_back(text("  \u2717 " + action.message) | color(Color::Red));
    }

    return vbox({
               hbox({
                   text(" Peer " + std::to_string(p.id) + " ") | bold | color(Color::Gold1),
                   filler(),
               }),
               separator(),
               vbox(std::move(detail_rows)),
           }) |
           border | size(WIDTH, EQUAL, 78);
}

// --- Add Node overlay -------------------------------------------------------
Element render_addnode_overlay(const AddNodeState& addnode, const std::string& addr_str) {
    static const char* kCmds[] = {"onetry", "add"};

    Elements rows;
    bool     done = addnode.pending || addnode.has_result;

    rows.push_back(hbox({
        text("  Command : ") | color(Color::GrayDark),
        text(kCmds[addnode.cmd_idx]) | color(Color::Yellow) | bold,
        done ? text("") : text("  [\u2190/\u2192 to change]") | color(Color::GrayDark),
        filler(),
    }));
    rows.push_back(hbox({
        text("  Address : ") | color(Color::GrayDark),
        text(addr_str) | color(Color::White),
        done ? text("") : text("\u2502") | color(Color::White),
        filler(),
    }));

    if (addnode.pending) {
        rows.push_back(separator());
        rows.push_back(text("  Connecting\u2026") | color(Color::Yellow));
    } else if (addnode.has_result) {
        rows.push_back(separator());
        if (addnode.success)
            rows.push_back(text("  \u2713 " + addnode.result_message) | color(Color::Green) | bold);
        else
            rows.push_back(text("  \u2717 Error: " + addnode.result_message) | color(Color::Red));
    }

    rows.push_back(text(""));
    rows.push_back(done ? text("  [Esc] close") | color(Color::GrayDark)
                        : text("  [Enter] submit  [Esc] cancel") | color(Color::GrayDark));

    return vbox({
               hbox({text(" Add Node ") | bold | color(Color::Gold1), filler()}),
               separator(),
               vbox(std::move(rows)),
           }) |
           border | size(WIDTH, EQUAL, 64);
}

// --- Ban / Unban Node overlay -----------------------------------------------
Element render_ban_overlay(const BanNodeState& ban, const std::string& addr_str) {
    Elements rows;
    bool     done = ban.pending || ban.has_result;

    rows.push_back(hbox({
        text("  Command : ") | color(Color::GrayDark),
        text(ban.is_remove ? "unban" : "ban  ") | color(Color::Yellow) | bold,
        done ? text("") : text("  [\u2190/\u2192 to toggle]") | color(Color::GrayDark),
        filler(),
    }));
    rows.push_back(hbox({
        text("  Address : ") | color(Color::GrayDark),
        text(addr_str) | color(Color::White),
        done ? text("") : text("\u2502") | color(Color::White),
        filler(),
    }));

    if (ban.pending) {
        rows.push_back(separator());
        rows.push_back(text("  Submitting\u2026") | color(Color::Yellow));
    } else if (ban.has_result) {
        rows.push_back(separator());
        if (ban.success)
            rows.push_back(text("  \u2713 " + ban.result_message) | color(Color::Green) | bold);
        else
            rows.push_back(text("  \u2717 Error: " + ban.result_message) | color(Color::Red));
    }

    rows.push_back(text(""));
    rows.push_back(text("  [Enter] submit  [Esc] cancel") | color(Color::GrayDark));

    return vbox({
               hbox({text(" Ban / Unban Node ") | bold | color(Color::Gold1), filler()}),
               separator(),
               vbox(std::move(rows)),
           }) |
           border | size(WIDTH, EQUAL, 64);
}

// --- Added Nodes overlay ----------------------------------------------------
Element render_added_nodes_panel(const std::vector<AddedNodeInfo>& nodes, bool loading,
                                 int selected) {
    Elements rows;
    rows.push_back(hbox({
        text(" Added Nodes ") | bold | color(Color::Gold1),
        filler(),
    }));
    rows.push_back(separator());

    Elements items;
    if (loading) {
        items.push_back(text("  Loading\u2026") | color(Color::GrayDark));
    } else if (nodes.empty()) {
        items.push_back(text("  (none)") | color(Color::GrayDark));
    } else {
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const auto& n   = nodes[i];
            auto        dot = n.connected ? text(" \u25cf ") | color(Color::Green)
                                          : text(" \u25cb ") | color(Color::GrayDark);
            auto        row = hbox({dot, text(n.addednode) | flex});
            if (i == selected)
                row = std::move(row) | inverted | focus;
            items.push_back(std::move(row));
        }
    }
    rows.push_back(vbox(std::move(items)) | yframe);
    rows.push_back(separator());
    rows.push_back(text("  [\u2191/\u2193] navigate  [\u23ce] remove  [a] add node  [Esc] close") |
                   color(Color::GrayDark));

    return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, 64);
}

// --- Ban List overlay -------------------------------------------------------
Element render_ban_list_panel(const std::vector<BannedEntry>& entries, bool loading, int selected) {
    auto now_secs = static_cast<int64_t>(std::time(nullptr));

    Elements rows;
    rows.push_back(hbox({
        text(" Ban List ") | bold | color(Color::Gold1),
        filler(),
    }));
    rows.push_back(separator());

    Elements items;
    if (loading) {
        items.push_back(text("  Loading\u2026") | color(Color::GrayDark));
    } else if (entries.empty()) {
        items.push_back(text("  (none)") | color(Color::GrayDark));
    } else {
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const auto& e         = entries[i];
            int64_t     remaining = e.banned_until - now_secs;
            Element     age_el;
            if (remaining > 0)
                age_el = text(fmt_age(remaining)) | color(Color::Default);
            else
                age_el = text("expired") | color(Color::GrayDark);
            auto row = hbox({text(" "), text(e.address) | flex, age_el, text(" ")});
            if (i == selected)
                row = std::move(row) | inverted | focus;
            items.push_back(std::move(row));
        }
    }
    rows.push_back(vbox(std::move(items)) | yframe);
    rows.push_back(separator());
    rows.push_back(text("  [\u2191/\u2193] navigate  [\u23ce] unban  [Esc] close") |
                   color(Color::GrayDark));

    return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, 64);
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
        constexpr int            kHexCols = 70;
        const auto&              h        = hex_str;
        int                      total    = (int)h.size();
        std::vector<std::string> chunks;
        for (int off = 0; off < std::max(total, 1); off += kHexCols)
            chunks.push_back(h.substr(off, std::min(kHexCols, total - off)));
        if (chunks.empty())
            chunks.push_back("");
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
            constexpr int      kErrCols   = 72;
            constexpr auto     kIndent    = "  ";
            constexpr int      kIndentLen = 2;
            std::istringstream ss(bs.result_error);
            std::string        word, cur = kIndent;
            while (ss >> word) {
                if ((int)cur.size() > kIndentLen &&
                    (int)(cur.size() + 1 + word.size()) > kErrCols) {
                    bcast_rows.push_back(text(cur) | color(Color::White));
                    cur = kIndent;
                }
                if ((int)cur.size() > kIndentLen)
                    cur += ' ';
                cur += word;
            }
            if ((int)cur.size() > kIndentLen)
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
