#include "peers.hpp"

#include "format.hpp"
#include "render.hpp"

using namespace ftxui;

PeersTab::PeersTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                   std::atomic<bool>& running, AppState& state, std::mutex& state_mtx)
    : Tab(std::move(cfg), auth, screen, running, state, state_mtx) {}

// ============================================================================
// Async actions
// ============================================================================

void PeersTab::trigger_peer_action(const std::string& addr, bool is_ban) {
    if (peer_action_in_flight.load())
        return;
    peer_action_in_flight = true;
    {
        STDLOCK(peer_action_mtx_);
        peer_action_ = PeerActionResult{};
    }
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
        {
            STDLOCK(peer_action_mtx_);
            peer_action_ = result;
        }
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::trigger_addnode(const std::string& addr, const std::string& cmd) {
    if (addnode_in_flight_.load())
        return;
    addnode_in_flight_ = true;
    {
        STDLOCK(addnode_mtx_);
        int saved_cmd          = addnode_state_.cmd_idx;
        addnode_state_         = AddNodeState{.pending = true};
        addnode_state_.cmd_idx = saved_cmd;
    }
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
        {
            STDLOCK(addnode_mtx_);
            addnode_state_ = result;
        }
        screen_.PostEvent(Event::Custom);
    });
}

void PeersTab::trigger_setban(const std::string& addr, bool remove) {
    if (ban_in_flight_.load())
        return;
    ban_in_flight_ = true;
    {
        STDLOCK(ban_mtx_);
        ban_state_ = BanNodeState{.pending = true};
    }
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
        {
            STDLOCK(ban_mtx_);
            ban_state_ = result;
        }
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
        {
            STDLOCK(added_nodes_mtx_);
            added_nodes_ = std::move(result);
        }
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
        {
            STDLOCK(banned_list_mtx_);
            banned_list_ = std::move(result);
        }
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
        PeerActionResult action_snap;
        {
            STDLOCK(peer_action_mtx_);
            action_snap = peer_action_;
        }
        bool    in_flight = peer_action_in_flight.load();
        Element msg;
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
        return vbox({filler(), hbox({filler(), msg | border, filler()}), filler()}) | flex;
    }

    if (peer_detail_open && peer_selected >= 0 &&
        peer_selected < static_cast<int>(snap.peers.size())) {
        PeerActionResult action_snap;
        {
            STDLOCK(peer_action_mtx_);
            action_snap = peer_action_;
        }
        return vbox({filler(),
                     hbox({filler(),
                           render_peer_detail(snap.peers[peer_selected], action_snap,
                                              peer_detail_sel_),
                           filler()}),
                     filler()}) |
               flex;
    }

    if (addnode_input_active) {
        AddNodeState addnode_snap;
        {
            STDLOCK(addnode_mtx_);
            addnode_snap = addnode_state_;
        }
        return vbox({filler(),
                     hbox({filler(), render_addnode_overlay(addnode_snap, addnode_str_), filler()}),
                     filler()}) |
               flex;
    }

    if (ban_input_active) {
        BanNodeState ban_snap;
        {
            STDLOCK(ban_mtx_);
            ban_snap = ban_state_;
        }
        return vbox({filler(), hbox({filler(), render_ban_overlay(ban_snap, ban_str_), filler()}),
                     filler()}) |
               flex;
    }

    if (peers_panel == 1) {
        if (!added_nodes_loaded_.load() && !added_nodes_loading_.load())
            fetch_added_nodes();
        std::vector<AddedNodeInfo> an_snap;
        {
            STDLOCK(added_nodes_mtx_);
            an_snap = added_nodes_;
        }
        return vbox({filler(),
                     hbox({filler(),
                           render_added_nodes_panel(an_snap, added_nodes_loading_.load(),
                                                    addednodes_sel_),
                           filler()}),
                     filler()}) |
               flex;
    }

    if (peers_panel == 2) {
        if (!banned_list_loaded_.load() && !banned_list_loading_.load())
            fetch_ban_list();
        std::vector<BannedEntry> bl_snap;
        {
            STDLOCK(banned_list_mtx_);
            bl_snap = banned_list_;
        }
        return vbox(
                   {filler(),
                    hbox({filler(),
                          render_ban_list_panel(bl_snap, banned_list_loading_.load(), banlist_sel_),
                          filler()}),
                    filler()}) |
               flex;
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
        {
            STDLOCK(addnode_mtx_);
            addnode_state_ = AddNodeState{};
        }
        screen_.PostEvent(Event::Custom);
        return true;
    }
    bool addnode_done;
    {
        STDLOCK(addnode_mtx_);
        addnode_done = addnode_state_.pending || addnode_state_.has_result;
    }
    if (!addnode_done) {
        if (event == Event::Return) {
            std::string addr = trimmed(addnode_str_);
            if (!addr.empty()) {
                static const char* cmds[] = {"onetry", "add"};
                AddNodeState       snap;
                {
                    STDLOCK(addnode_mtx_);
                    snap = addnode_state_;
                }
                trigger_addnode(addr, cmds[snap.cmd_idx]);
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
            {
                STDLOCK(addnode_mtx_);
                addnode_state_.cmd_idx = (addnode_state_.cmd_idx + 1) % 2;
            }
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
        {
            STDLOCK(ban_mtx_);
            ban_state_ = BanNodeState{};
        }
        screen_.PostEvent(Event::Custom);
        return true;
    }
    bool ban_done;
    {
        STDLOCK(ban_mtx_);
        ban_done = ban_state_.pending || ban_state_.has_result;
    }
    if (!ban_done) {
        if (event == Event::Return) {
            std::string addr = trimmed(ban_str_);
            if (!addr.empty()) {
                bool remove;
                {
                    STDLOCK(ban_mtx_);
                    remove = ban_state_.is_remove;
                }
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
            {
                STDLOCK(ban_mtx_);
                ban_state_.is_remove = !ban_state_.is_remove;
            }
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

bool PeersTab::handle_tab_events(const Event& event) {
    // peer_disconnect overlay — swallows all keys
    if (peer_disconnect_overlay) {
        if (event == Event::Escape && !peer_action_in_flight.load()) {
            peer_disconnect_overlay = false;
            {
                STDLOCK(peer_action_mtx_);
                peer_action_ = PeerActionResult{};
            }
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
            {
                STDLOCK(peer_action_mtx_);
                peer_action_ = PeerActionResult{};
            }
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
            std::string addr;
            {
                std::lock_guard lock(state_mtx_);
                if (peer_selected >= 0 && peer_selected < static_cast<int>(state_.peers.size()))
                    addr = state_.peers[peer_selected].addr;
            }
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
            STDLOCK(added_nodes_mtx_);
            int n = static_cast<int>(added_nodes_.size());
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
            std::string addr;
            {
                STDLOCK(added_nodes_mtx_);
                if (addednodes_sel_ < static_cast<int>(added_nodes_.size()))
                    addr = added_nodes_[addednodes_sel_].addednode;
            }
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
            STDLOCK(banned_list_mtx_);
            int n = static_cast<int>(banned_list_.size());
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
            std::string addr;
            {
                STDLOCK(banned_list_mtx_);
                if (banlist_sel_ < static_cast<int>(banned_list_.size()))
                    addr = banned_list_[banlist_sel_].address;
            }
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
        int n;
        {
            std::lock_guard lock(state_mtx_);
            n = static_cast<int>(state_.peers.size());
        }
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
        {
            STDLOCK(peer_action_mtx_);
            peer_action_ = PeerActionResult{};
        }
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
