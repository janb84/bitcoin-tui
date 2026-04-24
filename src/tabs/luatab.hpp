#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>

#include <ftxui/dom/elements.hpp>

#include "elements/qr_item.hpp"
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
    std::vector<QrItem> qr_items;
};

class LuaScript;
struct RpcRequest;
struct RpcResponse;

class LuaTab : public Tab {
  public:
    LuaTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
           std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
           std::string debug_log_path, json tab_options = {},
           std::span<const std::string> extra_rpcs = {}, std::ostream* debug_out = nullptr);
    ~LuaTab() override = default;

    std::string    name() const override;
    ftxui::Element render(const AppState& snap) override;
    FooterSpec     footer_buttons(const AppState& snap) override;
    bool           handle_focused_event(const ftxui::Event& event) override;
    void           join() override;

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
    mutable Guarded<std::deque<int>> btn_click_queue_;
    std::thread                      lua_thread_;
};
