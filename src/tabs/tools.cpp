#include "tools.hpp"

#include <algorithm>
#include <sstream>

#include "format.hpp"
#include "render.hpp"

using namespace ftxui;

static Element render_broadcast_input_overlay(const std::string& hex_str) {
    Elements rows;
    rows.push_back(text("  Paste raw transaction hex") | color(Color::GrayDark));
    rows.push_back(separator());

    constexpr int            kHexCols = 70;
    const auto&              h        = hex_str;
    int                      total    = static_cast<int>(h.size());
    std::vector<std::string> chunks;
    for (int off = 0; off < std::max(total, 1); off += kHexCols)
        chunks.push_back(h.substr(off, std::min(kHexCols, total - off)));
    if (chunks.empty())
        chunks.push_back("");

    for (int i = 0; i < static_cast<int>(chunks.size()); ++i) {
        bool is_last = i == static_cast<int>(chunks.size()) - 1;
        auto prefix  = i == 0 ? text("  Hex  : ") | color(Color::GrayDark)
                              : text("         ") | color(Color::GrayDark);
        rows.push_back(hbox({
            prefix,
            text(chunks[i]) | color(Color::White),
            is_last ? text("│") | color(Color::White) : text(""),
            filler(),
        }));
    }

    rows.push_back(separator());
    rows.push_back(text("  [Enter] submit  [Esc] cancel") | color(Color::GrayDark));

    auto panel = build_titled_panel(" Broadcast Transaction ", "", std::move(rows), 84);
    return center_overlay(std::move(panel));
}

static Element render_tools(const AppState& snap, const BroadcastState& bs, int tools_sel) {
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
    bcast_rows.push_back(menu_row("Broadcast", "[b]", tools_sel == 0));

    if (!bs.submitting) {
        bcast_rows.push_back(text(""));
        bcast_rows.push_back(
            text("  Submits to your node, which relays the transaction to all connected") |
            color(Color::GrayDark));
        bcast_rows.push_back(text("  peers over the P2P network.") | color(Color::GrayDark));
    }

    if (bs.submitting) {
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
        for (const auto& privbcast_txid : snap.privbcast_txids) {
            queue_rows.push_back(hbox({
                text("  ") | color(Color::GrayDark),
                text(privbcast_txid) | color(Color::White),
                filler(),
            }));
        }
        layout.push_back(section_box("Private Broadcast Queue", queue_rows));
    }

    // ── Node control ────────────────────────────────────────────────────────
    int      shutdown_idx = 1 + (has_result_row ? 1 : 0);
    Elements node_rows;
    node_rows.push_back(text(""));
    node_rows.push_back(menu_row("Shutdown bitcoind & exit", "[Q]", tools_sel == shutdown_idx));
    node_rows.push_back(text(""));
    node_rows.push_back(text("  Sends RPC stop to Bitcoin Core, then exits the TUI.") |
                        color(Color::GrayDark));
    layout.push_back(section_box("Shutdown", node_rows));

    layout.push_back(filler());
    return vbox(std::move(layout)) | flex;
}

ToolsTab::ToolsTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                   std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
                   std::function<void(const std::string&, bool)> trigger_search)
    : Tab(std::move(cfg), auth, screen, running, state, refresh_secs),
      trigger_search_(std::move(trigger_search)) {}

Element ToolsTab::key_hints(const AppState& /*snap*/) const {
    auto yellow_hint = [](const char* hint) { return hbox({text(hint) | color(Color::Yellow)}); };
    auto gray_hint   = [](const char* hint) { return hbox({text(hint) | color(Color::GrayDark)}); };

    if (tools_input_active)
        return yellow_hint("  [\u23ce] submit  [Esc] cancel ");
    return gray_hint("  [\u2191/\u2193] navigate  [\u23ce] select  [q] quit ");
}

void ToolsTab::open_broadcast_dialog() {
    tools_input_active = true;
    tools_sel          = 0;
    tools_hex_str_.clear();
    screen_.PostEvent(Event::Custom);
}

void ToolsTab::trigger_broadcast(const std::string& hex) {
    if (broadcast_in_flight_.load())
        return;
    broadcast_in_flight_ = true;
    broadcast_state_.update([&](auto& bs) { bs = BroadcastState{.hex = hex, .submitting = true}; });
    screen_.PostEvent(Event::Custom);
    if (broadcast_thread_.joinable())
        broadcast_thread_.join();
    broadcast_thread_ = std::thread([this, hex] {
        BroadcastState result{.hex = hex};
        try {
            RpcConfig bcast_cfg       = cfg_;
            bcast_cfg.timeout_seconds = 30;
            RpcClient bc(bcast_cfg, auth_);
            json      res      = bc.call("sendrawtransaction", {hex});
            result.result_txid = res["result"].get<std::string>();
            result.success     = true;
        } catch (const std::exception& e) {
            result.result_error = e.what();
            result.success      = false;
        }
        result.has_result    = true;
        broadcast_in_flight_ = false;
        if (!running_.load())
            return;
        broadcast_state_.update([&](auto& bs) { bs = result; });
        screen_.PostEvent(Event::Custom);
    });
}

void ToolsTab::do_shutdown() {
    try {
        RpcClient rpc(cfg_, auth_);
        rpc.call("stop", {});
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
    screen_.ExitLoopClosure()();
}

Element ToolsTab::render(const AppState& snap) {
    BroadcastState bs = broadcast_state_.get();
    if (tools_input_active)
        return render_broadcast_input_overlay(tools_hex_str_);
    return render_tools(snap, bs, tools_sel);
}

bool ToolsTab::handle_tools_input(const Event& event) {
    if (!tools_input_active)
        return false;
    if (event == Event::Escape) {
        tools_input_active = false;
        tools_hex_str_.clear();
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Return) {
        std::string hex    = trimmed(tools_hex_str_);
        tools_input_active = false;
        tools_hex_str_.clear();
        if (!hex.empty())
            trigger_broadcast(hex);
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Backspace) {
        if (!tools_hex_str_.empty())
            tools_hex_str_.pop_back();
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Tab || event == Event::TabReverse || event == Event::ArrowLeft ||
        event == Event::ArrowRight)
        return true;
    if (event.is_character()) {
        tools_hex_str_ += event.character();
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

bool ToolsTab::handle_focused_event(const Event& event) {
    bool has_result_row{false};
    broadcast_state_.access([&](const auto& bs) { has_result_row = bs.has_result && bs.success; });
    int shutdown_idx = 1 + (has_result_row ? 1 : 0);
    if (event == Event::Character('b')) {
        open_broadcast_dialog();
        return true;
    }
    if (event == Event::Character('Q')) {
        do_shutdown();
        return true;
    }
    if (event == Event::Return) {
        if (tools_sel == 0) {
            open_broadcast_dialog();
        } else if (tools_sel == shutdown_idx) {
            do_shutdown();
        } else if (tools_sel == 1 && has_result_row) {
            std::string txid;
            broadcast_state_.access([&](const auto& bs) {
                if (bs.has_result && bs.success)
                    txid = bs.result_txid;
            });
            if (!txid.empty())
                trigger_search_(txid, true);
        }
        return true;
    }
    if (event == Event::ArrowDown || event == Event::ArrowUp) {
        if (event == Event::ArrowDown)
            tools_sel = std::min(tools_sel + 1, shutdown_idx);
        else
            tools_sel = std::max(tools_sel - 1, 0);
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

void ToolsTab::join() {
    if (broadcast_thread_.joinable())
        broadcast_thread_.join();
}
