#pragma once

#include <atomic>

#include <ftxui/dom/elements.hpp>

#include "tabs/tab.hpp"

class SlowBlocksTab : public Tab {
  public:
    SlowBlocksTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
                  std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs);
    ~SlowBlocksTab() override = default;

    ftxui::Element render(const AppState& snap) override;
    ftxui::Element key_hints(const AppState& snap) const override;
    void           join() override;
};
