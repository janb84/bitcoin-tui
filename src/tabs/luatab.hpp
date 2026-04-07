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

#include "guarded.hpp"
#include "luatable.hpp"
#include "tabs/tab.hpp"

struct LuaError {
    std::string                           source_id; // e.g. "SLOWBLOCKS.lua:5"
    std::string                           message;
    std::chrono::system_clock::time_point when;
};

struct LuaTabState {
    std::string             lua_status; // status output from Lua
    std::string             tab_name;   // set by btcui_set_name()
    LuaTableVec             lua_tables;
    std::optional<LuaError> init_error;      // script load failure
    std::map<int, LuaError> callback_errors; // keyed by timer/watch id
    std::vector<LuaError>   warnings;        // age out after 20s
};

class LuaScript;
struct RpcRequest;
struct RpcResponse;

class LuaTab : public Tab {
  public:
    LuaTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
           std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
           std::string debug_log_path, std::string lua_script,
           std::span<const std::string> extra_rpcs = {});
    ~LuaTab() override = default;

    std::string    name() const override;
    ftxui::Element render(const AppState& snap) override;
    ftxui::Element key_hints(const AppState& snap) const override;
    void           join() override;

  private:
    void lua_thread_fn(std::unique_ptr<LuaScript> script);
    void rpc_thread_fn(WaitableGuarded<std::deque<RpcRequest>>&  requests,
                       WaitableGuarded<std::deque<RpcResponse>>& responses);
    void register_lua_api(LuaScript& script);
    void report_callback_error(int id, const std::string& source_id, const std::string& msg);
    void clear_callback_error(int id);

    const std::string           debug_log_path_;
    const std::set<std::string> rpc_allowlist_;
    Guarded<LuaTabState>        lua_tab_state_;
    std::thread                 lua_thread_;
};
