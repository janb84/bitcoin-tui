#pragma once

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "elements/footer_bar.hpp"
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

    virtual std::string    name() const                         = 0;
    virtual ftxui::Element render(const AppState& snap)         = 0;
    virtual FooterSpec     footer_buttons(const AppState& snap) = 0;
    virtual bool           handle_focused_event(const ftxui::Event&) { return false; }
    virtual void           join() = 0;

  protected:
    RpcConfig                 cfg_;
    Guarded<RpcAuth>&         auth_;
    ftxui::ScreenInteractive& screen_;
    std::atomic<bool>&        running_;
    Guarded<AppState>&        state_;
    int                       refresh_secs_;

    FooterButton refresh_btn(const AppState& snap) const {
        return {snap.refreshing ? " \u21bb refreshing"
                                : " \u21bb every " + std::to_string(refresh_secs_) + "s",
                nullptr, snap.refreshing};
    }
};
