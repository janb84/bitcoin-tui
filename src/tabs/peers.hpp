#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "rpc_client.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class PeersTab : public Tab {
  public:
    PeersTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
             std::atomic<bool>& running, AppState& state, std::mutex& state_mtx);
    ~PeersTab() override = default;

    ftxui::Element render(const AppState& snap) override;
    // Handles addnode input mode; call unconditionally (before tab navigation)
    bool handle_addnode_input(const ftxui::Event& event);
    // Handles ban input mode; call unconditionally (before tab navigation)
    bool handle_ban_input(const ftxui::Event& event);
    // Handles all peers tab events; call only when tab_index == 3
    bool handle_tab_events(const ftxui::Event& event);
    void join() override;

    // Public state read by main.cpp status bar and renderer
    int               peer_selected           = -1;
    bool              peer_detail_open        = false;
    bool              peer_disconnect_overlay = false;
    bool              addnode_input_active    = false;
    bool              ban_input_active        = false;
    int               peers_panel             = 0;
    std::atomic<bool> peer_action_in_flight{false};

  private:
    void trigger_peer_action(const std::string& addr, bool is_ban);
    void trigger_addnode(const std::string& addr, const std::string& cmd);
    void trigger_setban(const std::string& addr, bool remove);
    void fetch_added_nodes();
    void fetch_ban_list();
    void do_remove_added_node(const std::string& addr);
    void do_unban(const std::string& addr);

    int              peer_detail_sel_ = 0;
    StdMutex         peer_action_mtx_;
    PeerActionResult peer_action_ GUARDED_BY(peer_action_mtx_);
    std::thread      peer_action_thread_;

    int                        addednodes_sel_ = -1;
    StdMutex                   added_nodes_mtx_;
    std::vector<AddedNodeInfo> added_nodes_ GUARDED_BY(added_nodes_mtx_);
    std::atomic<bool>          added_nodes_loaded_{false};
    std::atomic<bool>          added_nodes_loading_{false};
    std::thread                added_nodes_thread_;
    std::string                addnode_str_;
    StdMutex                   addnode_mtx_;
    AddNodeState               addnode_state_ GUARDED_BY(addnode_mtx_);
    std::atomic<bool>          addnode_in_flight_{false};
    std::thread                addnode_thread_;
    std::thread                remove_addednode_thread_;

    int                      banlist_sel_ = -1;
    StdMutex                 banned_list_mtx_;
    std::vector<BannedEntry> banned_list_ GUARDED_BY(banned_list_mtx_);
    std::atomic<bool>        banned_list_loaded_{false};
    std::atomic<bool>        banned_list_loading_{false};
    std::thread              banned_list_thread_;
    std::string              ban_str_;
    StdMutex                 ban_mtx_;
    BanNodeState             ban_state_ GUARDED_BY(ban_mtx_);
    std::atomic<bool>        ban_in_flight_{false};
    std::thread              ban_thread_;
    std::thread              unban_action_thread_;
};
