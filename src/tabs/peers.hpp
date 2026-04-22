#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class PeersTab : public Tab {
  public:
    PeersTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
             std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs);
    ~PeersTab() override = default;

    std::string    name() const override { return "Peers"; }
    ftxui::Element render(const AppState& snap) override;
    ftxui::Element key_hints(const AppState& snap) const override;
    // Handles addnode input mode; call unconditionally (before tab navigation)
    bool handle_addnode_input(const ftxui::Event& event);
    // Handles ban input mode; call unconditionally (before tab navigation)
    bool handle_ban_input(const ftxui::Event& event);
    // Handles all peers tab events
    bool handle_focused_event(const ftxui::Event& event) override;
    void join() override;

  private:
    void trigger_peer_action(const std::string& addr, bool is_ban);
    void trigger_addnode(const std::string& addr, const std::string& cmd);
    void trigger_setban(const std::string& addr, bool remove);
    void fetch_added_nodes();
    void fetch_ban_list();
    void do_remove_added_node(const std::string& addr);
    void do_unban(const std::string& addr);

    int               peer_selected           = -1;
    bool              peer_detail_open        = false;
    bool              peer_disconnect_overlay = false;
    bool              addnode_input_active    = false;
    bool              ban_input_active        = false;
    int               peers_panel             = 0;
    std::atomic<bool> peer_action_in_flight{false};

    int                       peer_detail_sel_ = 0;
    Guarded<PeerActionResult> peer_action_;
    std::thread               peer_action_thread_;

    int                                 addednodes_sel_ = -1;
    Guarded<std::vector<AddedNodeInfo>> added_nodes_;
    std::atomic<bool>                   added_nodes_loaded_{false};
    std::atomic<bool>                   added_nodes_loading_{false};
    std::thread                         added_nodes_thread_;
    std::string                         addnode_str_;
    Guarded<AddNodeState>               addnode_state_;
    std::atomic<bool>                   addnode_in_flight_{false};
    std::thread                         addnode_thread_;
    std::thread                         remove_addednode_thread_;

    int                               banlist_sel_ = -1;
    Guarded<std::vector<BannedEntry>> banned_list_;
    std::atomic<bool>                 banned_list_loaded_{false};
    std::atomic<bool>                 banned_list_loading_{false};
    std::thread                       banned_list_thread_;
    std::string                       ban_str_;
    Guarded<BanNodeState>             ban_state_;
    std::atomic<bool>                 ban_in_flight_{false};
    std::thread                       ban_thread_;
    std::thread                       unban_action_thread_;
};
