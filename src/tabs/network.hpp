#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "rpc_client.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class NetworkTab : public Tab {
  public:
    NetworkTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
               std::atomic<bool>& running, AppState& state, std::mutex& state_mtx);
    ~NetworkTab() override = default;

    ftxui::Element render(const AppState& snap) override;
    void           join() override;

  private:
    void fetch();

    StdMutex                         mtx_;
    std::vector<SoftFork> softforks_ GUARDED_BY(mtx_);
    std::atomic<bool>                loaded_{false};
    std::atomic<bool>                loading_{false};
    std::thread                      thread_;
};
