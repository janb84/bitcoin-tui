#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/ftxui.hpp>

#include "components/dialog.hpp"
#include "components/footer_spec.hpp"
#include "components/hit_list.hpp"
#include "components/qr_item.hpp"
#include "guarded.hpp"
#include "json.hpp"
#include "luatable.hpp"
#include "rpc_client.hpp"

struct LuaError {
    std::string                           source_id; // e.g. "SLOWBLOCKS.lua:5"
    std::string                           message;
    std::chrono::system_clock::time_point when;
};

struct LuaPanelRender {
    explicit LuaPanelRender(std::shared_ptr<LuaPanel> p) : panel(std::move(p)) {}
    std::shared_ptr<LuaPanel> panel;
    std::atomic<int>          scroll_offset{0};
    std::atomic<bool>         scrollable{false};
    // Per-row screen rectangles from the last render, for mouse hit-testing.
    // UI-thread only (written by render(), read by handle_focused_event()).
    components::HitList row_hits;
};

using LuaPanelVec = std::vector<std::shared_ptr<LuaPanelRender>>;

struct LuaTabState {
    std::string             lua_status; // status output from Lua
    std::string             tab_name;   // set by btcui_set_name()
    LuaPanelVec             lua_panels;
    std::optional<LuaError> init_error;      // script load failure
    std::map<int, LuaError> callback_errors; // keyed by timer/watch id
    std::vector<LuaError>   warnings;        // age out after 20s
    // footer buttons registered by Lua via btcui_add_footer_button()
    struct FooterBtnInfo {
        int         id;
        std::string label;
        std::string key;
    };
    std::vector<FooterBtnInfo> footer_btn_labels;
    bool                       show_search = true;
    bool                       show_quit   = true;
    // qr component
    bool                show_qr_overlay = false;
    int                 qr_selected     = 0;
    std::vector<QrItem> qr_items;
    // text input overlay (btcui_text_input)
    struct InputOverlay {
        bool        active = false;
        std::string label;
        std::string buffer;
        int         cursor = 0;
    };
    InputOverlay input_overlay;
    // modal dialog overlay (btcui_dialog)
    components::DialogState dialog;
};

class LuaScript;
struct RpcRequest;
struct RpcResponse;

// Hosts one Lua tab script: owns its worker threads, the btcui_* API bindings and
// the UI state the script drives. Every tab in the app is one of these; there is
// no tab abstraction above it.
class LuaTab {
  public:
    LuaTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::App& screen, std::atomic<bool>& running,
           int refresh_secs, std::string debug_log_path, json tab_options = {},
           std::span<const std::string> extra_rpcs = {}, std::ostream* debug_out = nullptr);

    std::string    name() const;
    ftxui::Element render();
    // `refreshing` drives the footer's refresh indicator (owned by the poll loop).
    FooterSpec footer_buttons(bool refreshing);
    bool       handle_focused_event(const ftxui::Event& event);
    void       join();

    // Tell the tab whether it is the one on screen. A hidden tab stops firing its
    // btcui_set_interval timers (and so stops issuing RPCs) until it is shown
    // again, at which point its timers are due and fire on the next loop pass.
    // Tabs declared `background=true` in their tab spec keep running while hidden.
    void set_visible(bool visible);

    std::string script_path() const;
    void        set_reload_callback(std::function<void()> fn);
    // Exit the whole TUI (btcui_quit). Wired by main to ExitLoopClosure.
    void set_quit_callback(std::function<void()> fn);
    // Run the global transaction search for `query` (btcui_search). Wired by main
    // to the tab that registered btcui_on_search (switching to that tab).
    void set_search_callback(std::function<void(const std::string&)> fn);

    // True once the script has registered a btcui_on_search handler (the bundled
    // Mempool tab). main routes global-search queries to the first such tab.
    bool handles_search() const { return has_search_handler_.load(); }
    // Queue a search query for the script's btcui_on_search callback.
    void trigger_search(const std::string& query);

    // Per-instance shutdown, independent of the shared `running` flag. Used when a
    // tab is de-loaded at runtime: the worker threads wind down within ~1s.
    void stop();
    // True once the worker thread has fully exited (so join() returns immediately).
    bool finished() const;

  private:
    void lua_thread_fn(std::unique_ptr<LuaScript> script);
    void rpc_thread_fn(WaitableGuarded<std::deque<RpcRequest>>&  requests,
                       WaitableGuarded<std::deque<RpcResponse>>& responses);
    void register_lua_api(LuaScript& script);
    void report_callback_error(int id, const std::string& source_id, const std::string& msg);
    void clear_callback_error(int id);
    void open_qr_overlay(const std::string& data);

    const RpcConfig             cfg_;
    Guarded<RpcAuth>&           auth_;
    ftxui::App&                 screen_;
    std::atomic<bool>&          running_;
    const int                   refresh_secs_;
    std::ostream* const         debug_out_;
    const std::string           debug_log_path_;
    const json                  tab_options_;
    const std::set<std::string> rpc_allowlist_;
    // Timer gating: hidden tabs idle instead of polling the node. `background_`
    // opts a tab out (set once from the tab spec, so it is not atomic).
    std::atomic<bool>                       visible_{true};
    bool                                    background_{false};
    Guarded<LuaTabState>                    lua_tab_state_;
    std::atomic<int>                        focused_panel_{-1};
    std::atomic<bool>                       panel_scrolling_{false};
    std::atomic<bool>                       stopped_{false};     // per-tab shutdown request
    std::atomic<bool>                       thread_done_{false}; // set when lua_thread_ exits
    std::function<void()>                   reload_request_fn_;
    std::function<void()>                   quit_request_fn_;
    std::function<void(const std::string&)> search_request_fn_;
    std::atomic<bool>                       has_search_handler_{false};
    mutable Guarded<std::deque<int>>        btn_click_queue_;
    mutable Guarded<std::deque<std::optional<std::string>>> input_result_queue_;
    // Search queries awaiting dispatch to the btcui_on_search callback.
    mutable Guarded<std::deque<std::string>> search_query_queue_;
    // Row activations awaiting dispatch: (row key, trigger). Trigger is "enter",
    // "space", or "click" so the Lua callback can treat activate (Enter/click)
    // and toggle (Space) differently.
    mutable Guarded<std::deque<std::pair<std::string, std::string>>> select_queue_;
    // Dialog activations/keypresses awaiting dispatch to the Lua thread.
    mutable Guarded<std::deque<components::DialogEvent>> dialog_event_queue_;
    // Dialog button/item rectangles from the last render, for mouse hit-testing.
    // UI-thread only (written by render(), read by handle_focused_event()).
    components::DialogHits dialog_hits_;
    std::atomic<bool>      resize_pending_{false};
    std::atomic<int>       last_dimx_{0};
    std::atomic<int>       last_dimy_{0};
    std::thread            lua_thread_;
};
