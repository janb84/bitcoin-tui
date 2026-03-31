#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class NetworkTab : public Tab {
  public:
    NetworkTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
               std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs);
    ~NetworkTab() override = default;

    ftxui::Element render(const AppState& snap) override;
    ftxui::Element key_hints(const AppState& snap) const override;
    void           join() override;

  private:
    void fetch();

    Guarded<std::vector<SoftFork>> softforks_;
    std::atomic<bool>              loaded_{false};
    std::atomic<bool>              loading_{false};
    std::thread                    thread_;
};
