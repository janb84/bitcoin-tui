#pragma once

#include <atomic>
#include <mutex>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "rpc_client.hpp"
#include "state.hpp"

class Tab {
  public:
    Tab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
        std::atomic<bool>& running, AppState& state, std::mutex& state_mtx)
        : cfg_{std::move(cfg)}, auth_{auth}, screen_{screen}, running_{running}, state_{state}, state_mtx_{state_mtx} {}
    virtual ~Tab() = default;

    virtual ftxui::Element render(const AppState& snap) = 0;
    virtual void           join()                       = 0;

  protected:
    RpcConfig                 cfg_;
    Guarded<RpcAuth>&         auth_;
    ftxui::ScreenInteractive& screen_;
    std::atomic<bool>&        running_;
    std::mutex&               state_mtx_;
    AppState&                 state_;
};
