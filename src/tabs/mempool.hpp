#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "rpc_client.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

struct MempoolOverlayInfo {
    bool visible         = false;
    bool is_confirmed_tx = false;
    bool block_row_sel   = false;
    bool inputs_row_sel  = false;
    bool outputs_row_sel = false;
    bool inputs_open     = false;
    bool outputs_open    = false;
};

class MempoolTab : public Tab {
  public:
    MempoolTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
               std::atomic<bool>& running, AppState& state, std::mutex& state_mtx);
    ~MempoolTab() override = default;

    // switch_tab=true sets tab_index_out=1 before launching the search thread
    void trigger_search(const std::string& query, bool switch_tab, int& tab_index_out);

    MempoolOverlayInfo overlay_info() const;
    ftxui::Element     render(const AppState& snap) override;
    // Handles outputs/inputs sub-overlays; nullopt = not in overlay mode
    std::optional<bool> handle_tx_overlay(const ftxui::Event& event);
    // Handles mempool block navigation; call only when tab_index == 1
    bool handle_navigation(const ftxui::Event& event);
    // Handles io_selected arrow navigation for confirmed tx (normal mode)
    bool handle_io_nav(const ftxui::Event& event);
    // Handles Enter for tx block/io drill-down
    bool handle_enter(const ftxui::Event& event);
    // Handles Escape: history pop or overlay dismiss; returns true if handled
    bool handle_escape(const ftxui::Event& event);
    void join() override;

    int mempool_sel = -1;

  private:
    mutable StdMutex           search_mtx_;
    TxSearchState              search_state_ GUARDED_BY(search_mtx_);
    std::vector<TxSearchState> search_history_ GUARDED_BY(search_mtx_);
    std::atomic<bool>          search_in_flight_{false};
    std::thread                search_thread_;
};
