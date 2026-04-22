#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class ToolsTab : public Tab {
  public:
    ToolsTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
             std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
             std::function<void(const std::string&, bool)> trigger_search);
    ~ToolsTab() override = default;

    std::string    name() const override { return "Tools"; }
    ftxui::Element render(const AppState& snap) override;
    ftxui::Element key_hints(const AppState& snap) const override;
    // Handles tools_input_active mode; call unconditionally (before tab navigation)
    bool handle_tools_input(const ftxui::Event& event);
    // Handles tab 4 key navigation
    bool handle_focused_event(const ftxui::Event& event) override;
    void join() override;

  private:
    void open_broadcast_dialog();
    void trigger_broadcast(const std::string& hex);
    void do_shutdown();

    std::function<void(const std::string&, bool)> trigger_search_;

    bool tools_input_active = false;
    int  tools_sel          = 0;

    Guarded<BroadcastState> broadcast_state_;
    std::atomic<bool>       broadcast_in_flight_{false};
    std::string             tools_hex_str_;
    std::thread             broadcast_thread_;
};
