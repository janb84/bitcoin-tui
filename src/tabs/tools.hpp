#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "rpc_client.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class ToolsTab : public Tab {
  public:
    ToolsTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
             std::atomic<bool>& running, AppState& state, std::mutex& state_mtx,
             std::function<void(const std::string&, bool)> trigger_search);
    ~ToolsTab() override = default;

    ftxui::Element render(const AppState& snap) override;
    // Handles tools_input_active mode; call unconditionally (before tab navigation)
    bool handle_tools_input(const ftxui::Event& event);
    // Handles tab 4 key navigation; call only when tab_index == 4
    bool handle_keys(const ftxui::Event& event);
    void join() override;

    bool tools_input_active = false;
    int  tools_sel          = 0;

  private:
    void open_broadcast_dialog();
    void trigger_broadcast(const std::string& hex);
    void do_shutdown();

    std::function<void(const std::string&, bool)> trigger_search_;

    StdMutex          broadcast_mtx_;
    BroadcastState    broadcast_state_ GUARDED_BY(broadcast_mtx_);
    std::atomic<bool> broadcast_in_flight_{false};
    std::string       tools_hex_str_;
    std::thread       broadcast_thread_;
};
