#pragma once

#include <atomic>
#include <string>
#include <utility>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "rpc_client.hpp"
#include "state.hpp"

class Tab {
  public:
    Tab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
        std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs)
        : cfg_{std::move(cfg)}, auth_{auth}, screen_{screen}, running_{running}, state_{state},
          refresh_secs_{refresh_secs} {}
    virtual ~Tab() = default;

    virtual std::string    name() const                          = 0;
    virtual ftxui::Element render(const AppState& snap)          = 0;
    virtual ftxui::Element key_hints(const AppState& snap) const = 0;
    virtual void           join()                                = 0;

    static ftxui::Element refresh_indicator(const AppState& snap, int refresh_secs) {
        return snap.refreshing
                   ? ftxui::text(" \u21bb refreshing") | ftxui::color(ftxui::Color::Yellow)
                   : ftxui::text(" \u21bb every " + std::to_string(refresh_secs) + "s") |
                         ftxui::color(ftxui::Color::GrayDark);
    }

  protected:
    RpcConfig                 cfg_;
    Guarded<RpcAuth>&         auth_;
    ftxui::ScreenInteractive& screen_;
    std::atomic<bool>&        running_;
    Guarded<AppState>&        state_;
    int                       refresh_secs_;

    ftxui::Element refresh_indicator(const AppState& snap) const {
        return refresh_indicator(snap, refresh_secs_);
    }
};
