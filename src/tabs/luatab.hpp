#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>

#include <ftxui/dom/elements.hpp>

#include "components/hit_list.hpp"
#include "components/qr_item.hpp"
#include "guarded.hpp"
#include "json.hpp"
#include "luatable.hpp"
#include "tabs/tab.hpp"

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
};

class LuaScript;
struct RpcRequest;
struct RpcResponse;

class LuaTab : public Tab {
  public:
    LuaTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::App& screen, std::atomic<bool>& running,
           Guarded<AppState>& state, int refresh_secs, std::string debug_log_path,
           json tab_options = {}, std::span<const std::string> extra_rpcs = {},
           std::ostream* debug_out = nullptr);
    ~LuaTab() override = default;

    std::string    name() const override;
    ftxui::Element render(const AppState& snap) override;
    FooterSpec     footer_buttons(const AppState& snap) override;
    bool           handle_focused_event(const ftxui::Event& event) override;
    void           join() override;

    std::string script_path() const;
    void        set_reload_callback(std::function<void()> fn);

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

    const std::string                debug_log_path_;
    const json                       tab_options_;
    const std::set<std::string>      rpc_allowlist_;
    Guarded<LuaTabState>             lua_tab_state_;
    std::atomic<int>                 focused_panel_{-1};
    std::atomic<bool>                panel_scrolling_{false};
    std::atomic<bool>                stopped_{false};     // per-tab shutdown request
    std::atomic<bool>                thread_done_{false}; // set when lua_thread_ exits
    std::function<void()>            reload_request_fn_;
    mutable Guarded<std::deque<int>> btn_click_queue_;
    mutable Guarded<std::deque<std::optional<std::string>>> input_result_queue_;
    mutable Guarded<std::deque<std::string>>                select_queue_;
    std::atomic<bool>                                       resize_pending_{false};
    std::atomic<int>                                        last_dimx_{0};
    std::atomic<int>                                        last_dimy_{0};
    std::thread                                             lua_thread_;
};
