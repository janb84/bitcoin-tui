#include "peers.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "format.hpp"
#include "render.hpp"

using namespace ftxui;

static Element render_peers(const AppState& s, int selected) {
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

static Element render_peer_detail(const PeerInfo& p, const PeerActionResult& action, int sel) {
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

static Element render_addnode_overlay(const AddNodeState& addnode, const std::string& addr_str) {
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

static Element render_ban_overlay(const BanNodeState& ban, const std::string& addr_str) {
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

static Element render_added_nodes_panel(const std::vector<AddedNodeInfo>& nodes, bool loading,
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

static Element render_ban_list_panel(const std::vector<BannedEntry>& entries, bool loading,
                                     int selected) {
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

PeersTab::PeersTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                   std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs)
    : Tab(std::move(cfg), auth, screen, running, state, refresh_secs) {}

Element PeersTab::key_hints(const AppState& snap) const {
    auto yellow_hint = [](const char* hint) { return hbox({text(hint) | color(Color::Yellow)}); };
    auto gray_hint   = [](const char* hint) { return hbox({text(hint) | color(Color::GrayDark)}); };

    struct HintCase {
        bool        enabled;
        bool        yellow;
        const char* hint;
    };
    const HintCase overlay_hints[] = {
        {addnode_input_active, true, "  [\u23ce] submit  [\u2190/\u2192] change  [Esc] cancel "},
        {ban_input_active, true,
         "  [\u23ce] submit  [\u2190/\u2192] ban\u2215unban  [Esc] cancel "},
        {peer_disconnect_overlay && !peer_action_in_flight.load(), true,
         "  [Esc] dismiss  [q] quit "},
        {peer_disconnect_overlay, false, "  [q] quit "},
        {peers_panel == 1, true,
         "  [\u2191/\u2193] navigate  [\u23ce] remove  [a] add node  [Esc] close  [q] quit "},
        {peers_panel == 2, true,
         "  [\u2191/\u2193] navigate  [\u23ce] unban  [Esc] close  [q] quit "},
        {peer_detail_open, true,
         "  [\u2190/\u2192] select action  [\u23ce] confirm  [Esc] back  [q] quit "},
    };

    for (const auto& item : overlay_hints) {
        if (item.enabled)
            return item.yellow ? yellow_hint(item.hint) : gray_hint(item.hint);
    }

    auto ri = refresh_indicator(snap);
    if (peer_selected >= 0)
        return hbox({ri, text("  [\u2191/\u2193] navigate  [\u23ce] details  [a] added nodes  "
                              "[b] ban list  [q] quit ") |
                             color(Color::GrayDark)});
    return hbox({ri, text("  [\u2191/\u2193] navigate  [a] added nodes  [b] ban list  "
                          "[q] quit ") |
                         color(Color::GrayDark)});
}

// ============================================================================
// Async actions
// ============================================================================

void PeersTab::trigger_peer_action(const std::string& addr, bool is_ban) {
    if (peer_action_in_flight.load())
        return;
    peer_action_in_flight = true;
    peer_action_          = PeerActionResult{};
    screen_.PostEvent(Event::Custom);
    if (peer_action_thread_.joinable())
        peer_action_thread_.join();
    peer_action_thread_ = std::thread([this, addr, is_ban] {
        PeerActionResult result;
        try {
            RpcClient rc(cfg_, auth_);
            if (is_ban) {
                std::string ip = addr;
                if (!addr.empty() && addr[0] == '[') {
                    auto end = addr.find(']');
                    if (end != std::string::npos)
                        ip = addr.substr(0, end + 1);
                } else {
                    auto colon = addr.rfind(':');
                    if (colon != std::string::npos)
                        ip = addr.substr(0, colon);
                }
                rc.call("setban", {ip, std::string("add")});
                result.message = "Banned " + ip;
            } else {
                rc.call("disconnectnode", {addr});
                result.message = "Disconnected";
            }
            result.success = true;
        } catch (const std::exception& e) {
            result.message = e.what();
        }
        result.has_result     = true;
        peer_action_in_flight = false;
        if (is_ban)
            banned_list_loaded_ = false;
        if (!running_.load())
            return;
        peer_action_ = std::move(result);
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::trigger_addnode(const std::string& addr, const std::string& cmd) {
    if (addnode_in_flight_.load())
        return;
    addnode_in_flight_ = true;
    addnode_state_.update([](auto& s) {
        int saved_cmd = s.cmd_idx;
        s             = AddNodeState{.pending = true};
        s.cmd_idx     = saved_cmd;
    });
    screen_.PostEvent(Event::Custom);
    if (addnode_thread_.joinable())
        addnode_thread_.join();
    addnode_thread_ = std::thread([this, addr, cmd] {
        AddNodeState result;
        try {
            RpcClient rc(cfg_, auth_);
            rc.call("addnode", {addr, cmd});
            result.success        = true;
            result.result_message = cmd + " " + addr;
        } catch (const std::exception& e) {
            result.result_message = e.what();
        }
        result.has_result   = true;
        addnode_in_flight_  = false;
        added_nodes_loaded_ = false;
        if (!running_.load())
            return;
        addnode_state_ = std::move(result);
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::trigger_setban(const std::string& addr, bool remove) {
    if (ban_in_flight_.load())
        return;
    ban_in_flight_ = true;
    ban_state_     = BanNodeState{.pending = true};
    screen_.PostEvent(Event::Custom);
    if (ban_thread_.joinable())
        ban_thread_.join();
    ban_thread_ = std::thread([this, addr, remove] {
        BanNodeState result;
        try {
            RpcClient   rc(cfg_, auth_);
            std::string cmd = remove ? "remove" : "add";
            rc.call("setban", {addr, cmd});
            result.success        = true;
            result.result_message = (remove ? "Unbanned " : "Banned ") + addr;
        } catch (const std::exception& e) {
            result.result_message = e.what();
        }
        result.has_result   = true;
        ban_in_flight_      = false;
        banned_list_loaded_ = false;
        if (!running_.load())
            return;
        ban_state_ = std::move(result);
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::fetch_added_nodes() {
    if (added_nodes_loading_.load())
        return;
    if (added_nodes_thread_.joinable())
        added_nodes_thread_.join();
    added_nodes_loading_ = true;
    added_nodes_thread_  = std::thread([this] {
        std::vector<AddedNodeInfo> result;
        try {
            RpcClient rc(cfg_, auth_);
            auto      ans = rc.call("getaddednodeinfo")["result"];
            for (const auto& n : ans) {
                AddedNodeInfo info;
                info.addednode = n.value("addednode", "");
                if (n.contains("addresses") && n["addresses"].is_array()) {
                    for (const auto& a : n["addresses"]) {
                        if (a.value("connected", false)) {
                            info.connected = true;
                            break;
                        }
                    }
                }
                result.push_back(std::move(info));
            }
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
        if (!running_.load())
            return;
        added_nodes_         = std::move(result);
        added_nodes_loading_ = false;
        added_nodes_loaded_  = true;
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::fetch_ban_list() {
    if (banned_list_loading_.load())
        return;
    if (banned_list_thread_.joinable())
        banned_list_thread_.join();
    banned_list_loading_ = true;
    banned_list_thread_  = std::thread([this] {
        std::vector<BannedEntry> result;
        try {
            RpcClient rc(cfg_, auth_);
            auto      bl = rc.call("listbanned")["result"];
            for (const auto& b : bl) {
                BannedEntry entry;
                entry.address      = b.value("address", "");
                entry.banned_until = b.value("banned_until", 0LL);
                entry.ban_reason   = b.value("ban_reason", "");
                result.push_back(std::move(entry));
            }
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
        if (!running_.load())
            return;
        banned_list_         = std::move(result);
        banned_list_loading_ = false;
        banned_list_loaded_  = true;
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::do_remove_added_node(const std::string& addr) {
    if (remove_addednode_thread_.joinable())
        remove_addednode_thread_.join();
    remove_addednode_thread_ = std::thread([this, addr] {
        try {
            RpcClient rc(cfg_, auth_);
            rc.call("addnode", {addr, std::string("remove")});
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
        if (!running_.load())
            return;
        added_nodes_loaded_  = false;
        added_nodes_loading_ = false;
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::do_unban(const std::string& addr) {
    if (unban_action_thread_.joinable())
        unban_action_thread_.join();
    unban_action_thread_ = std::thread([this, addr] {
        try {
            RpcClient rc(cfg_, auth_);
            rc.call("setban", {addr, std::string("remove")});
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
        if (!running_.load())
            return;
        banned_list_loaded_  = false;
        banned_list_loading_ = false;
        screen_.PostEvent(Event::Custom);
    });
}

// ============================================================================
// Render
// ============================================================================

Element PeersTab::render(const AppState& snap) {
    if (peer_disconnect_overlay) {
        PeerActionResult action_snap = peer_action_.get();
        bool             in_flight   = peer_action_in_flight.load();
        Element          msg;
        if (in_flight || !action_snap.has_result) {
            msg = text("  Working\u2026  ") | bold | color(Color::Yellow);
        } else if (action_snap.success) {
            msg = vbox({
                text("  \u2713 " + action_snap.message + "  ") | bold | color(Color::Green),
                text("  [Esc] to dismiss  ") | color(Color::GrayDark),
            });
        } else {
            msg = vbox({
                text("  \u2717 " + action_snap.message + "  ") | bold | color(Color::Red),
                text("  [Esc] to dismiss  ") | color(Color::GrayDark),
            });
        }
        return center_overlay(msg | border);
    }

    if (peer_detail_open && peer_selected >= 0 &&
        peer_selected < static_cast<int>(snap.peers.size())) {
        PeerActionResult action_snap = peer_action_.get();
        return center_overlay(
            render_peer_detail(snap.peers[peer_selected], action_snap, peer_detail_sel_));
    }

    if (addnode_input_active) {
        AddNodeState addnode_snap = addnode_state_.get();
        return center_overlay(render_addnode_overlay(addnode_snap, addnode_str_));
    }

    if (ban_input_active) {
        BanNodeState ban_snap = ban_state_.get();
        return center_overlay(render_ban_overlay(ban_snap, ban_str_));
    }

    if (peers_panel == 1) {
        if (!added_nodes_loaded_.load() && !added_nodes_loading_.load())
            fetch_added_nodes();
        std::vector<AddedNodeInfo> an_snap = added_nodes_.get();
        return center_overlay(
            render_added_nodes_panel(an_snap, added_nodes_loading_.load(), addednodes_sel_));
    }

    if (peers_panel == 2) {
        if (!banned_list_loaded_.load() && !banned_list_loading_.load())
            fetch_ban_list();
        std::vector<BannedEntry> bl_snap = banned_list_.get();
        return center_overlay(
            render_ban_list_panel(bl_snap, banned_list_loading_.load(), banlist_sel_));
    }

    return render_peers(snap, peer_selected) | flex;
}

// ============================================================================
// Event handling
// ============================================================================

bool PeersTab::handle_addnode_input(const Event& event) {
    if (!addnode_input_active)
        return false;
    if (event == Event::Escape) {
        addnode_input_active = false;
        addnode_str_.clear();
        addnode_state_ = AddNodeState{};
        screen_.PostEvent(Event::Custom);
        return true;
    }
    bool addnode_done =
        addnode_state_.access([](const auto& s) { return s.pending || s.has_result; });
    if (!addnode_done) {
        if (event == Event::Return) {
            std::string addr = trimmed(addnode_str_);
            if (!addr.empty()) {
                static const char* cmds[] = {"onetry", "add"};
                int cmd_idx = addnode_state_.access([](const auto& s) { return s.cmd_idx; });
                trigger_addnode(addr, cmds[cmd_idx]);
            }
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Backspace) {
            if (!addnode_str_.empty())
                addnode_str_.pop_back();
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowLeft || event == Event::ArrowRight) {
            addnode_state_.update([](auto& s) { s.cmd_idx = (s.cmd_idx + 1) % 2; });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Tab || event == Event::TabReverse)
            return true;
        if (event.is_character()) {
            addnode_str_ += event.character();
            screen_.PostEvent(Event::Custom);
            return true;
        }
    }
    return true; // swallow all other keys while overlay is open
}

bool PeersTab::handle_ban_input(const Event& event) {
    if (!ban_input_active)
        return false;
    if (event == Event::Escape) {
        ban_input_active = false;
        ban_str_.clear();
        ban_state_ = BanNodeState{};
        screen_.PostEvent(Event::Custom);
        return true;
    }
    bool ban_done = ban_state_.access([](const auto& s) { return s.pending || s.has_result; });
    if (!ban_done) {
        if (event == Event::Return) {
            std::string addr = trimmed(ban_str_);
            if (!addr.empty()) {
                bool remove = ban_state_.access([](const auto& s) { return s.is_remove; });
                trigger_setban(addr, remove);
            }
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Backspace) {
            if (!ban_str_.empty())
                ban_str_.pop_back();
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowLeft || event == Event::ArrowRight) {
            ban_state_.update([](auto& s) { s.is_remove = !s.is_remove; });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Tab || event == Event::TabReverse)
            return true;
        if (event.is_character()) {
            ban_str_ += event.character();
            screen_.PostEvent(Event::Custom);
            return true;
        }
    }
    return true; // swallow all other keys while overlay is open
}

bool PeersTab::handle_focused_event(const Event& event) {
    // peer_disconnect overlay — swallows all keys
    if (peer_disconnect_overlay) {
        if (event == Event::Escape && !peer_action_in_flight.load()) {
            peer_disconnect_overlay = false;
            peer_action_            = PeerActionResult{};
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Character('q')) {
            screen_.ExitLoopClosure()();
            return true;
        }
        return true; // swallow all other keys
    }

    // peer_detail overlay
    if (peer_detail_open) {
        if (event == Event::Escape) {
            peer_detail_open = false;
            peer_action_     = PeerActionResult{};
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowLeft || event == Event::ArrowRight || event == Event::ArrowUp ||
            event == Event::ArrowDown) {
            peer_detail_sel_ = 1 - peer_detail_sel_;
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Return || event == Event::Character('d') ||
            event == Event::Character('b')) {
            int action_sel = peer_detail_sel_;
            if (event == Event::Character('d'))
                action_sel = 0;
            else if (event == Event::Character('b'))
                action_sel = 1;
            std::string addr = state_.access([&](const auto& s) -> std::string {
                if (peer_selected >= 0 && peer_selected < static_cast<int>(s.peers.size()))
                    return s.peers[peer_selected].addr;
                return {};
            });
            if (!addr.empty()) {
                peer_detail_open        = false;
                peer_disconnect_overlay = true;
                trigger_peer_action(addr, action_sel == 1);
            }
            return true;
        }
        if (event == Event::Character('q')) {
            screen_.ExitLoopClosure()();
            return true;
        }
        return false;
    }

    // Added Nodes panel
    if (peers_panel == 1) {
        if (event == Event::Escape) {
            peers_panel = 0;
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowDown || event == Event::ArrowUp) {
            int n = added_nodes_.access([](const auto& v) { return static_cast<int>(v.size()); });
            if (n > 0) {
                if (addednodes_sel_ < 0)
                    addednodes_sel_ = 0;
                else if (event == Event::ArrowDown)
                    addednodes_sel_ = std::min(addednodes_sel_ + 1, n - 1);
                else
                    addednodes_sel_ = std::max(addednodes_sel_ - 1, 0);
            }
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Return && addednodes_sel_ >= 0) {
            std::string addr = added_nodes_.access([&](const auto& v) -> std::string {
                if (addednodes_sel_ < static_cast<int>(v.size()))
                    return v[addednodes_sel_].addednode;
                return {};
            });
            if (!addr.empty()) {
                do_remove_added_node(addr);
                addednodes_sel_ = -1;
            }
            return true;
        }
        if (event == Event::Character('a')) {
            addnode_input_active = true;
            addnode_str_.clear();
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Character('q')) {
            screen_.ExitLoopClosure()();
            return true;
        }
        return false;
    }

    // Ban List panel
    if (peers_panel == 2) {
        if (event == Event::Escape) {
            peers_panel = 0;
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowDown || event == Event::ArrowUp) {
            int n = banned_list_.access([](const auto& v) { return static_cast<int>(v.size()); });
            if (n > 0) {
                if (banlist_sel_ < 0)
                    banlist_sel_ = 0;
                else if (event == Event::ArrowDown)
                    banlist_sel_ = std::min(banlist_sel_ + 1, n - 1);
                else
                    banlist_sel_ = std::max(banlist_sel_ - 1, 0);
            }
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Return && banlist_sel_ >= 0) {
            std::string addr = banned_list_.access([&](const auto& v) -> std::string {
                if (banlist_sel_ < static_cast<int>(v.size()))
                    return v[banlist_sel_].address;
                return {};
            });
            if (!addr.empty()) {
                do_unban(addr);
                banlist_sel_ = -1;
            }
            return true;
        }
        if (event == Event::Character('q')) {
            screen_.ExitLoopClosure()();
            return true;
        }
        return false;
    }

    // General peer list navigation
    if (event == Event::ArrowDown || event == Event::ArrowUp) {
        int n = state_.access([](const auto& s) { return static_cast<int>(s.peers.size()); });
        if (n > 0) {
            if (event == Event::ArrowDown)
                peer_selected = std::min(peer_selected + 1, n - 1);
            else
                peer_selected = std::max(peer_selected - 1, 0);
        }
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Return && peer_selected >= 0) {
        peer_detail_open = true;
        peer_detail_sel_ = 0;
        peer_action_     = PeerActionResult{};
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Character('a')) {
        peers_panel     = 1;
        addednodes_sel_ = -1;
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Character('b')) {
        peers_panel  = 2;
        banlist_sel_ = -1;
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

void PeersTab::join() {
    if (peer_action_thread_.joinable())
        peer_action_thread_.join();
    if (addnode_thread_.joinable())
        addnode_thread_.join();
    if (ban_thread_.joinable())
        ban_thread_.join();
    if (remove_addednode_thread_.joinable())
        remove_addednode_thread_.join();
    if (unban_action_thread_.joinable())
        unban_action_thread_.join();
    if (added_nodes_thread_.joinable())
        added_nodes_thread_.join();
    if (banned_list_thread_.joinable())
        banned_list_thread_.join();
}
