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

class NetworkTab {
  public:
    NetworkTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
               std::atomic<bool>& running);
    ~NetworkTab() = default;

    ftxui::Element render(const AppState& snap);
    void           join();

  private:
    void fetch();

    RpcConfig                 cfg_;
    Guarded<RpcAuth>&         auth_;
    ftxui::ScreenInteractive& screen_;
    std::atomic<bool>&        running_;

    StdMutex                 mtx_;
    std::vector<SoftFork> softforks_ GUARDED_BY(mtx_);
    std::atomic<bool>     loaded_{false};
    std::atomic<bool>     loading_{false};
    std::thread           thread_;
};
