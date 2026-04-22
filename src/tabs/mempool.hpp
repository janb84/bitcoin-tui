#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class MempoolTab : public Tab {
  public:
    MempoolTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
               std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs);
    ~MempoolTab() override = default;

    // switch_tab=true sets tab_index_out=1 before launching the search thread
    void trigger_search(const std::string& query, bool switch_tab, int& tab_index_out);

    std::string    name() const override { return "Mempool"; }
    ftxui::Element render(const AppState& snap) override;
    FooterSpec     footer_buttons(const AppState& snap) override;
    // Handles outputs/inputs sub-overlays; nullopt = not in overlay mode
    std::optional<bool> handle_tx_overlay(const ftxui::Event& event);
    // Handles mempool block navigation
    bool handle_focused_event(const ftxui::Event& event) override;
    // Handles io_selected arrow navigation for confirmed tx (normal mode)
    bool handle_io_nav(const ftxui::Event& event);
    // Handles Enter for tx block/io drill-down
    bool handle_enter(const ftxui::Event& event);
    // Handles Escape: history pop or overlay dismiss; returns true if handled
    bool handle_escape(const ftxui::Event& event);
    void join() override;

  private:
    struct OverlayInfo {
        bool visible         = false;
        bool is_confirmed_tx = false;
        bool block_row_sel   = false;
        bool inputs_row_sel  = false;
        bool outputs_row_sel = false;
        bool inputs_open     = false;
        bool outputs_open    = false;
    };

    OverlayInfo overlay_info() const;
    int         mempool_sel = -1;
    struct SearchData {
        TxSearchState              state;
        std::vector<TxSearchState> history;
    };
    mutable Guarded<SearchData> search_data_;
    std::atomic<bool>           search_in_flight_{false};
    std::thread                 search_thread_;
};
