#pragma once

#include <atomic>

#include <ftxui/dom/elements.hpp>

#include "tabs/tab.hpp"

class DashboardTab : public Tab {
  public:
    DashboardTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
                 std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs);
    ~DashboardTab() override = default;

    std::string    name() const override { return "Dashboard"; }
    ftxui::Element render(const AppState& snap) override;
    FooterSpec     footer_buttons(const AppState& snap) override;
    void           join() override;
};
