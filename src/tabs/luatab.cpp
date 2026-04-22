#include "luatab.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <re2/re2.h>
#include <sol/sol.hpp>

#include "format.hpp"
#include "luatable.hpp"
#include "render.hpp"

using namespace ftxui;
using Clock     = std::chrono::system_clock;
using TimePoint = Clock::time_point;

static const std::set<std::string> DEFAULT_RPC_ALLOWLIST = {
    "decoderawtransaction",  "decodescript",      "estimatesmartfee",
    "getbestblockhash",      "getblock",          "getblockchaininfo",
    "getblockcount",         "getblockhash",      "getblockheader",
    "getblockstats",         "getchaintips",      "getconnectioncount",
    "getdeploymentinfo",     "getindexinfo",      "getmempoolancestors",
    "getmempooldescendants", "getmempoolentry",   "getmempoolinfo",
    "getmininginfo",         "getnettotals",      "getnetworkhashps",
    "getnetworkinfo",        "getnodeaddresses",  "getpeerinfo",
    "getrawmempool",         "getrawtransaction", "gettxout",
    "gettxoutsetinfo",       "logging",           "uptime",
};

struct RpcRequest {
    int                        id;
    std::string                method;
    json                       params;
    std::optional<std::string> wallet; // empty = regular RPC, set = wallet RPC
};

struct RpcResponse {
    int         id;
    json        result;
    std::string error;
};

namespace {

std::string lua_source_id(lua_State* L) {
    lua_Debug ar;
    if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar)) {
        std::string src = ar.source;
        if (!src.empty() && src[0] == '@')
            src = src.substr(1);
        return src + ":" + std::to_string(ar.currentline);
    }
    return "unknown";
}

struct LogWatch {
    int                     id;
    RE2                     pattern;
    int                     ngroups;
    int64_t                 backlog_bytes;
    sol::protected_function callback;
    std::string             source_id;
    LogWatch(int id, const std::string& pat, sol::protected_function fn, std::string src,
             int64_t backlog = 0)
        : id(id), pattern(pat), ngroups(pattern.NumberOfCapturingGroups()),
          backlog_bytes(std::max(int64_t{0}, backlog)), callback(std::move(fn)),
          source_id(std::move(src)) {}
};

struct TimerHandle {
    int id;
};

struct LuaTimer {
    int                     id;
    Clock::duration         interval;
    sol::protected_function callback;
    std::string             source_id;
};

struct PendingCoroutine {
    sol::thread    lua_thread;
    sol::coroutine coro;
    LuaTimer       timer;
    bool           wake_pending = false;
};

} // namespace

class LuaScript {
  public:
    LuaScript();

    sol::protected_function_result load(const std::string& script_path);

    sol::state&                             lua() { return lua_; }
    std::vector<std::unique_ptr<LogWatch>>& log_watches() { return log_watches_; }
    std::map<TimePoint, LuaTimer>&          timers() { return timers_; }

    void        add_log_watch(const std::string& pattern, sol::protected_function fn,
                              std::string source_id, int64_t backlog);
    TimerHandle add_timer(Clock::duration interval, sol::protected_function fn,
                          std::string source_id);
    void        wake(const TimerHandle& h);
    std::map<int, PendingCoroutine>& pending() { return pending_; }

    sol::object json_to_lua(const json& j);

    static CellData  to_cell_data(ColumnType type, int decimals, const sol::object& v);
    static CellValue to_cell_value(ColumnType type, int decimals, const sol::object& v);
    static CellData  to_key(LuaTable& self, const sol::object& v);

    std::vector<LuaError>& warnings() { return warnings_; }

    void add_warning(std::string source_id, std::string msg) {
        warnings_.push_back({std::move(source_id), std::move(msg), Clock::now()});
    }

  private:
    sol::state                             lua_;
    std::vector<std::unique_ptr<LogWatch>> log_watches_;
    std::map<TimePoint, LuaTimer>          timers_;
    int                                    next_callback_id_ = 0;
    std::map<int, PendingCoroutine>        pending_;
    std::vector<LuaError>                  warnings_;
};

LuaScript::LuaScript() {
    lua_.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math,
                        sol::lib::coroutine);
    lua_.script(R"(
        function btcui_rpc(method, ...)
            local result, err = coroutine.yield('rpc', method, {...})
            if err then error(err, 2) end
            return result
        end
        function btcui_rpc_wallet(wallet, method, ...)
            local result, err = coroutine.yield('rpc_wallet', wallet, method, {...})
            if err then error(err, 2) end
            return result
        end
    )");
}

void LuaScript::add_log_watch(const std::string& pattern, sol::protected_function fn,
                              std::string source_id, int64_t backlog) {
    int id = ++next_callback_id_;
    log_watches_.push_back(
        std::make_unique<LogWatch>(id, pattern, std::move(fn), std::move(source_id), backlog));
}

TimerHandle LuaScript::add_timer(Clock::duration interval, sol::protected_function fn,
                                 std::string source_id) {
    int id = ++next_callback_id_;
    timers_.insert({Clock::now(), {id, interval, std::move(fn), std::move(source_id)}});
    return {id};
}

void LuaScript::wake(const TimerHandle& h) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->second.id == h.id) {
            auto node  = timers_.extract(it);
            node.key() = TimePoint::min();
            timers_.insert(std::move(node));
            return;
        }
    }
    for (auto& [id, pc] : pending_) {
        if (pc.timer.id == h.id) {
            pc.wake_pending = true;
            return;
        }
    }
    throw std::runtime_error("btcui_wake: invalid timer handle");
}

sol::protected_function_result LuaScript::load(const std::string& script_path) {
    return lua_.safe_script_file(script_path, sol::script_pass_on_error);
}

sol::object LuaScript::json_to_lua(const json& j) {
    if (j.is_null())
        return sol::make_object(lua_, sol::lua_nil);
    if (j.is_bool())
        return sol::make_object(lua_, j.get<bool>());
    if (j.is_number_integer())
        return sol::make_object(lua_, j.get<int64_t>());
    if (j.is_number_float())
        return sol::make_object(lua_, j.get<double>());
    if (j.is_string())
        return sol::make_object(lua_, j.get<std::string>());
    if (j.is_array()) {
        sol::table t = lua_.create_table(static_cast<int>(j.size()), 0);
        for (size_t i = 0; i < j.size(); ++i)
            t[i + 1] = json_to_lua(j[i]);
        return t;
    }
    if (j.is_object()) {
        sol::table t = lua_.create_table(0, static_cast<int>(j.size()));
        for (auto& [k, v] : j.items())
            t[k] = json_to_lua(v);
        return t;
    }
    return sol::make_object(lua_, sol::lua_nil);
}

CellData LuaScript::to_cell_data(ColumnType type, int decimals, const sol::object& v) {
    switch (type) {
    case ColumnType::Number:
        if (decimals >= 0) {
            if (v.is<double>())
                return v.as<double>();
            if (v.is<int64_t>())
                return static_cast<double>(v.as<int64_t>());
            return 0.0;
        }
        if (v.is<int64_t>())
            return v.as<int64_t>();
        if (v.is<double>())
            return static_cast<int64_t>(v.as<double>());
        return int64_t(0);
    case ColumnType::DateTime:
    case ColumnType::Date:
    case ColumnType::Time:
    case ColumnType::TimeMS:
        if (v.is<double>())
            return v.as<double>();
        return 0.0;
    default:
        if (v.is<std::string>())
            return v.as<std::string>();
        if (v.is<double>())
            return std::to_string(v.as<double>());
        return std::string{};
    }
}

CellValue LuaScript::to_cell_value(ColumnType type, int decimals, const sol::object& v) {
    CellValue cv;
    if (v.is<sol::table>()) {
        sol::table sv = v;
        cv.color      = sv.get_or<std::string>("color", "");
        cv.bold       = sv.get_or("bold", false);
        cv.data       = to_cell_data(type, decimals, sv["value"]);
    } else {
        cv.data = to_cell_data(type, decimals, v);
    }
    return cv;
}

CellData LuaScript::to_key(LuaTable& self, const sol::object& v) {
    return to_cell_data(self.key_type(), -1, v);
}

void LuaTab::register_lua_api(LuaScript& script) {
    auto& lua_ = script.lua();

    lua_["btcui_error"] = [&script](sol::this_state ts, const std::string& msg) {
        script.add_warning(lua_source_id(ts.L), msg);
    };
    lua_.new_usertype<LuaTable>(
        "LuaTable", "update",
        [](LuaTable& self, const sol::object& key, const sol::table& data) {
            std::map<std::string, CellValue> cells;
            const auto&                      cols = self.columns();
            for (auto& [k, v] : data) {
                if (v.is<sol::lua_nil_t>())
                    continue;
                std::string col_name = k.as<std::string>();
                CellValue   cv;
                ColumnType  ct  = ColumnType::String;
                int         dec = -1;
                for (const auto& col : cols) {
                    if (col.name == col_name) {
                        ct  = col.type;
                        dec = col.decimals;
                        break;
                    }
                }
                cells[col_name] = LuaScript::to_cell_value(ct, dec, v);
            }
            self.update(LuaScript::to_key(self, key), cells);
        },
        "remove",
        [](LuaTable& self, const sol::object& key) {
            return self.remove(LuaScript::to_key(self, key));
        },
        "keys", &LuaTable::keys, "start_refresh", &LuaTable::start_refresh, "finish_refresh",
        &LuaTable::finish_refresh, "set_header_info",
        [](LuaTable& self, const sol::object& v) {
            self.set_header_info(LuaScript::to_cell_value(ColumnType::String, -1, v));
        });

    lua_["btcui_watch_log"] = [&script](const std::string& pattern, sol::protected_function fn,
                                        sol::optional<int64_t> backlog) {
        auto src = lua_source_id(fn.lua_state());
        script.add_log_watch(pattern, std::move(fn), std::move(src), backlog.value_or(0));
    };

    lua_["btcui_table"] = [this](sol::table opts) -> std::shared_ptr<LuaTable> {
        sol::table             col_defs = opts["columns"];
        std::vector<ColumnDef> cols;
        for (size_t i = 1; i <= col_defs.size(); ++i) {
            sol::table  col      = col_defs[i];
            std::string name     = col["name"];
            std::string header   = col.get_or<std::string>("header", name);
            std::string type_str = col.get_or<std::string>("type", "string");
            auto        type     = parse_column_type(type_str);
            if (!type) {
                throw std::runtime_error("unknown column type: " + type_str);
            }
            int decimals = col.get_or("decimals", -1);
            cols.push_back({std::move(name), std::move(header), *type, decimals});
        }
        std::string def_key    = cols.empty() ? std::string{} : cols[0].name;
        std::string key_column = opts.get_or("key", std::move(def_key));
        std::string title      = opts.get_or("title", std::string{});
        bool        no_header  = opts.get_or("no_header", false);
        auto        tbl =
            std::make_shared<LuaTable>(key_column, std::move(cols), std::move(title), no_header);
        lua_tab_state_.update([&](auto& st) { st.lua_panels.push_back(tbl); });
        return tbl;
    };

    lua_.new_usertype<LuaSummary>("LuaSummary", "set",
                                  [](LuaSummary& self, const sol::table& data) {
                                      std::map<std::string, CellValue> cells;
                                      const auto&                      flds = self.fields();
                                      for (auto& [k, v] : data) {
                                          if (v.is<sol::lua_nil_t>())
                                              continue;
                                          std::string field_name = k.as<std::string>();
                                          ColumnType  ct         = ColumnType::String;
                                          int         dec        = -1;
                                          for (const auto& f : flds) {
                                              if (f.name == field_name) {
                                                  ct  = f.type;
                                                  dec = f.decimals;
                                                  break;
                                              }
                                          }
                                          cells[field_name] = LuaScript::to_cell_value(ct, dec, v);
                                      }
                                      self.set(cells);
                                  });

    lua_["btcui_summary"] = [this](sol::table opts) -> std::shared_ptr<LuaSummary> {
        sol::table             field_defs = opts["fields"];
        std::vector<ColumnDef> fields;
        size_t                 max_label = 0;
        for (size_t i = 1; i <= field_defs.size(); ++i) {
            sol::table  f        = field_defs[i];
            std::string name     = f["name"];
            std::string label    = f.get_or<std::string>("label", name);
            std::string type_str = f.get_or<std::string>("type", "string");
            auto        type     = parse_column_type(type_str);
            if (!type) {
                throw std::runtime_error("unknown field type: " + type_str);
            }
            if (label.size() > max_label)
                max_label = label.size();
            int decimals = f.get_or("decimals", -1);
            fields.push_back({std::move(name), std::move(label), *type, decimals});
        }
        // Pad labels so colons align in the rendered summary
        for (auto& f : fields)
            f.header.resize(max_label, ' ');

        std::string title = opts.get_or("title", std::string{});
        auto        sum   = std::make_shared<LuaSummary>(std::move(fields), std::move(title));
        lua_tab_state_.update([&](auto& st) { st.lua_panels.push_back(sum); });
        return sum;
    };

    lua_["btcui_key_hint"] = [this](const std::string& hint) {
        lua_tab_state_.update([&](auto& st) { st.lua_status = hint; });
    };

    lua_.new_usertype<TimerHandle>("TimerHandle", sol::no_constructor);

    lua_["btcui_set_interval"] = [&script](double secs, sol::protected_function fn) -> TimerHandle {
        auto src = lua_source_id(fn.lua_state());
        auto interval =
            std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(secs));
        return script.add_timer(interval, std::move(fn), std::move(src));
    };

    lua_["btcui_wake"] = [&script](const TimerHandle& h) { script.wake(h); };

    lua_["btcui_set_name"] = [this](const std::string& name) {
        lua_tab_state_.update([&](auto& st) { st.tab_name = name; });
    };

    lua_["btcui_option"] = [this](sol::this_state ts, const std::string& key,
                                  sol::optional<sol::object> default_val) -> sol::object {
        if (tab_options_.contains(key))
            return sol::make_object(ts, tab_options_[key].get<std::string>());
        if (default_val)
            return *default_val;
        throw std::runtime_error("required tab option '" + key + "' not set");
    };
}

void LuaTab::rpc_thread_fn(WaitableGuarded<std::deque<RpcRequest>>&  requests,
                           WaitableGuarded<std::deque<RpcResponse>>& responses) {
    while (running_) {
        auto req = requests.access_when([&](auto& q) { return !q.empty() || !running_; },
                                        [](auto& q) -> std::optional<RpcRequest> {
                                            if (q.empty())
                                                return std::nullopt;
                                            auto r = std::move(q.front());
                                            q.pop_front();
                                            return r;
                                        });
        if (!req)
            continue;

        RpcResponse resp{req->id, {}, {}};
        try {
            RpcClient rpc(cfg_, auth_);
            json      rpc_result;
            if (!req->wallet.has_value()) {
                rpc_result = rpc.call(req->method, req->params);
            } else {
                rpc_result = rpc.call_wallet(*req->wallet, req->method, req->params);
            }
            resp.result = rpc_result["result"];
        } catch (const std::exception& e) {
            resp.error = e.what();
        }

        responses.update_and_notify([&](auto& q) { q.push_back(std::move(resp)); });
    }
}

static json extract_rpc_params(const sol::table& args) {
    std::vector<json> pv;
    for (size_t i = 1; i <= args.size(); ++i) {
        sol::object a = args[i];
        if (a.is<int64_t>())
            pv.emplace_back(a.as<int64_t>());
        else if (a.is<double>())
            pv.emplace_back(a.as<double>());
        else if (a.is<bool>())
            pv.emplace_back(a.as<bool>());
        else if (a.is<std::string>())
            pv.emplace_back(a.as<std::string>());
    }
    return json(std::move(pv));
}

void LuaTab::lua_thread_fn(std::unique_ptr<LuaScript> script) {
    auto& lua         = script->lua();
    auto& log_watches = script->log_watches();
    auto& timers      = script->timers();

    WaitableGuarded<std::deque<RpcRequest>>  requests;
    WaitableGuarded<std::deque<RpcResponse>> responses;
    int                                      next_rpc_id = 0;
    auto&                                    pending     = script->pending();

    std::thread rpc_thread(&LuaTab::rpc_thread_fn, this, std::ref(requests), std::ref(responses));

    // Open debug.log, seek back by max backlog
    int64_t max_backlog = 0;
    for (const auto& lw : log_watches) {
        max_backlog = std::max(max_backlog, lw->backlog_bytes);
    }

    std::ifstream logfile(debug_log_path_);
    int64_t       live_pos = 0;
    if (logfile) {
        logfile.seekg(0, std::ios::end);
        live_pos = logfile.tellg();
        if (max_backlog > 0 && live_pos > max_backlog) {
            logfile.seekg(live_pos - max_backlog);
            std::string discard;
            std::getline(logfile, discard);
        }
    }

    auto wake_ui = [this] { screen_.PostEvent(ftxui::Event::Custom); };

    auto submit_rpc = [&](const std::string& method, json params,
                          std::optional<std::string> wallet = std::nullopt) -> int {
        int id = ++next_rpc_id;
        requests.update_and_notify(
            [&](auto& q) { q.push_back({id, method, std::move(params), std::move(wallet)}); });
        return id;
    };

    // Resume a coroutine with a value. If it yields another RPC,
    // submit that and return the new rpc_id. If it finishes,
    // return nullopt (caller reschedules timer).
    auto resume_coro = [&](PendingCoroutine& pc, const sol::object& value,
                           const sol::object& err) -> std::optional<int> {
        auto result = pc.coro(value, err);
        while (pc.coro.status() == sol::call_status::yielded) {
            std::string tag = result;
            if (tag == "rpc" && result.return_count() >= 2) {
                std::string method = result.get<std::string>(1);
                if (!rpc_allowlist_.contains(method)) {
                    result = pc.coro(sol::lua_nil, "RPC method not allowed: " + method);
                    continue;
                }
                return submit_rpc(method, extract_rpc_params(result.get<sol::table>(2)));
            } else if (tag == "rpc_wallet" && result.return_count() >= 3) {
                std::string wallet = result.get<std::string>(1);
                std::string method = result.get<std::string>(2);
                if (!rpc_allowlist_.contains(method)) {
                    result = pc.coro(sol::lua_nil, "RPC method not allowed: " + method);
                    continue;
                }
                return submit_rpc(method, extract_rpc_params(result.get<sol::table>(3)), wallet);
            } else {
                break;
            }
        }
        if (!result.valid()) {
            sol::error err = result;
            report_callback_error(pc.timer.id, pc.timer.source_id, err.what());
        } else {
            clear_callback_error(pc.timer.id);
        }
        return std::nullopt;
    };

    std::string line;
    while (running_) {
        // 1. Read new log lines, feed to Lua callbacks
        if (logfile) {
            while (std::getline(logfile, line)) {
                int64_t          cur_pos         = logfile.tellg();
                int64_t          bytes_from_live = std::max(int64_t{0}, live_pos - cur_pos);
                static const RE2 re_ts_msg(
                    R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,6}))?Z (.*)$)");
                std::string y, mo, d, h, mi, s, frac, msg;
                double      ts = 0.0;
                if (RE2::FullMatch(line, re_ts_msg, &y, &mo, &d, &h, &mi, &s, &frac, &msg)) {
                    auto ymd = std::chrono::year{std::stoi(y)} /
                               std::chrono::month{static_cast<unsigned>(std::stoi(mo))} /
                               std::chrono::day{static_cast<unsigned>(std::stoi(d))};
                    Clock::time_point tp =
                        std::chrono::sys_days{ymd} + std::chrono::hours{std::stoi(h)} +
                        std::chrono::minutes{std::stoi(mi)} + std::chrono::seconds{std::stoi(s)};
                    if (!frac.empty()) {
                        while (frac.size() < 6)
                            frac += '0';
                        tp += std::chrono::microseconds(std::stoi(frac));
                    }
                    ts = std::chrono::duration<double>(tp.time_since_epoch()).count();
                } else {
                    msg = line;
                }

                for (auto& lw : log_watches) {
                    if (bytes_from_live > lw->backlog_bytes)
                        continue;
                    int                          n = lw->ngroups;
                    std::vector<std::string>     captures(n);
                    std::vector<RE2::Arg>        args(n);
                    std::vector<const RE2::Arg*> arg_ptrs(n);
                    for (int i = 0; i < n; ++i) {
                        args[i]     = &captures[i];
                        arg_ptrs[i] = &args[i];
                    }
                    if (RE2::PartialMatchN(msg, lw->pattern, arg_ptrs.data(), n)) {
                        sol::variadic_results vr;
                        vr.push_back({lua, sol::in_place, ts});
                        vr.push_back({lua, sol::in_place, msg});
                        for (int i = 0; i < n; ++i) {
                            vr.push_back({lua, sol::in_place, captures[i]});
                        }
                        auto result = lw->callback(std::move(vr));
                        if (!result.valid()) {
                            sol::error err = result;
                            report_callback_error(lw->id, lw->source_id, err.what());
                        } else {
                            clear_callback_error(lw->id);
                        }
                    }
                }
            }
            logfile.clear();
        }

        // 2. Collect RPC responses and resume waiting coroutines
        auto resp_queue = responses.update([](auto& q) { return std::exchange(q, {}); });
        for (auto& resp : resp_queue) {
            auto it = pending.find(resp.id);
            if (it == pending.end())
                continue;

            auto&       pc    = it->second;
            sol::object value = resp.error.empty() ? script->json_to_lua(resp.result)
                                                   : sol::make_object(lua, sol::lua_nil);
            sol::object err   = resp.error.empty() ? sol::make_object(lua, sol::lua_nil)
                                                   : sol::make_object(lua, resp.error);

            auto new_rpc_id = resume_coro(pc, value, err);
            if (new_rpc_id) {
                auto node  = pending.extract(it);
                node.key() = *new_rpc_id;
                pending.insert(std::move(node));
            } else {
                auto next = pc.wake_pending ? TimePoint::min() : Clock::now() + pc.timer.interval;
                timers.insert({next, std::move(pc.timer)});
                pending.erase(it);
            }
        }

        // 3. Fire due timers
        auto now = Clock::now();
        while (!timers.empty() && timers.begin()->first <= now) {
            auto  node  = timers.extract(timers.begin());
            auto& timer = node.mapped();

            sol::thread    thread = sol::thread::create(lua);
            sol::coroutine coro(thread.state(), timer.callback);

            auto result = coro();
            if (coro.status() == sol::call_status::yielded) {
                std::string tag = result;
                if (tag == "rpc" && result.return_count() >= 2) {
                    std::string method = result.get<std::string>(1);
                    if (!rpc_allowlist_.contains(method)) {
                        result = coro(sol::lua_nil, "RPC method not allowed: " + method);
                    } else {
                        int rpc_id =
                            submit_rpc(method, extract_rpc_params(result.get<sol::table>(2)));
                        pending.emplace(rpc_id, PendingCoroutine{std::move(thread), std::move(coro),
                                                                 std::move(timer)});
                        now = Clock::now();
                        continue; // timer NOT rescheduled yet
                    }
                } else if (tag == "rpc_wallet" && result.return_count() >= 3) {
                    std::string wallet = result.get<std::string>(1);
                    std::string method = result.get<std::string>(2);
                    if (!rpc_allowlist_.contains(method)) {
                        result = coro(sol::lua_nil, "RPC method not allowed: " + method);
                    } else {
                        int rpc_id = submit_rpc(
                            method, extract_rpc_params(result.get<sol::table>(3)), wallet);
                        pending.emplace(rpc_id, PendingCoroutine{std::move(thread), std::move(coro),
                                                                 std::move(timer)});
                        now = Clock::now();
                        continue; // timer NOT rescheduled yet
                    }
                }
            }
            if (!result.valid()) {
                sol::error err = result;
                report_callback_error(timer.id, timer.source_id, err.what());
            } else {
                clear_callback_error(timer.id);
            }
            now        = Clock::now();
            node.key() = std::max(now, node.key() + timer.interval);
            timers.insert(std::move(node));
        }

        // 4. Flush warnings into shared state, expire old ones
        auto& warns  = script->warnings();
        auto  cutoff = Clock::now() - std::chrono::seconds(20);
        lua_tab_state_.update([&](auto& st) {
            for (auto& w : warns)
                st.warnings.push_back(std::move(w));
            std::erase_if(st.warnings, [&](const auto& w) { return w.when < cutoff; });
        });
        warns.clear();

        // 5. Wake UI
        wake_ui();

        // 5. Sleep — wait for RPC response or next timer, cap at 1s for log polling
        auto deadline = Clock::now() + std::chrono::seconds(1);
        if (!timers.empty())
            deadline = std::min(deadline, timers.begin()->first);
        responses.wait_until(deadline, [](auto& q) { return !q.empty(); });
    }

    // Shutdown: wake rpc thread so it sees !running_
    requests.notify();
    rpc_thread.join();
}

static std::set<std::string> make_allowlist(std::span<const std::string> extra) {
    auto allowlist = DEFAULT_RPC_ALLOWLIST;
    allowlist.insert(extra.begin(), extra.end());
    return allowlist;
}

LuaTab::LuaTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
               std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
               std::string debug_log_path, json tab_options,
               std::span<const std::string> extra_rpcs)
    : Tab(std::move(cfg), auth, screen, running, state, refresh_secs),
      debug_log_path_(std::move(debug_log_path)), tab_options_(std::move(tab_options)),
      rpc_allowlist_(make_allowlist(extra_rpcs)) {
    const std::string lua_script = tab_options_["script"].get<std::string>();
    auto              script     = std::make_unique<LuaScript>();
    lua_tab_state_.update(
        [&](auto& st) { st.tab_name = std::filesystem::path(lua_script).stem().string(); });
    register_lua_api(*script);
    auto result = script->load(lua_script);
    if (!result.valid()) {
        sol::error err = result;
        lua_tab_state_.update(
            [&](auto& st) { st.init_error = LuaError{lua_script, err.what(), Clock::now()}; });
        return;
    }
    script->lua()["btcui_set_name"] = [](const std::string&) {
        throw std::runtime_error("btcui_set_name() can only be called during script loading");
    };
    lua_thread_ = std::thread(&LuaTab::lua_thread_fn, this, std::move(script));
}

std::string LuaTab::name() const {
    if (tab_options_.contains("t"))
        return tab_options_["t"].get<std::string>();
    return lua_tab_state_.access([](const auto& s) { return s.tab_name; });
}

void LuaTab::report_callback_error(int id, const std::string& source_id, const std::string& msg) {
    lua_tab_state_.update(
        [&](auto& st) { st.callback_errors[id] = {source_id, msg, Clock::now()}; });
}

void LuaTab::clear_callback_error(int id) {
    lua_tab_state_.update([&](auto& st) { st.callback_errors.erase(id); });
}

Element LuaTab::key_hints(const AppState& snap) const {
    auto lua_str = lua_tab_state_.access([](const auto& s) { return s.lua_status; });
    return hbox({text("  " + lua_str) | color(Color::Cyan), refresh_indicator(snap),
                 text("  [Tab/\u2190/\u2192] switch  [q] quit ") | color(Color::GrayDark)});
}

static Element apply_style(Element el, const CellValue& cv) {
    if (!cv.color.empty()) {
        if (cv.color == "red")
            el = el | color(Color::Red);
        else if (cv.color == "green")
            el = el | color(Color::Green);
        else if (cv.color == "yellow")
            el = el | color(Color::Yellow);
        else if (cv.color == "cyan")
            el = el | color(Color::Cyan);
        else if (cv.color == "gray")
            el = el | color(Color::GrayDark);
    }
    if (cv.bold)
        el = el | ftxui::bold;
    return el;
}

Element LuaTab::render(const AppState& /*snap*/) {
    Elements    lua_elems;
    LuaPanelVec panels_vec = lua_tab_state_.access([](const auto& s) { return s.lua_panels; });

    // Collect runs of consecutive summaries for side-by-side layout
    Elements summary_run;
    auto     flush_summaries = [&]() {
        if (summary_run.empty())
            return;
        lua_elems.push_back(hbox(std::move(summary_run)));
        summary_run.clear();
    };

    for (const auto& panel : panels_vec) {
        if (auto tbl = std::dynamic_pointer_cast<LuaTable>(panel)) {
            flush_summaries();

            const auto& cols  = tbl->columns();
            size_t      ncols = cols.size();

            // Visible columns (non-empty header)
            std::vector<size_t> vis;
            for (size_t i = 0; i < ncols; ++i) {
                if (!cols[i].header.empty())
                    vis.push_back(i);
            }

            // Compute column widths for visible columns
            std::vector<int> widths(vis.size());
            for (size_t vi = 0; vi < vis.size(); ++vi) {
                const auto& hdr   = cols[vis[vi]].header;
                int         max_w = 0;
                size_t      pos   = 0;
                while (pos <= hdr.size()) {
                    size_t nl = hdr.find('\n', pos);
                    if (nl == std::string::npos)
                        nl = hdr.size();
                    int w = static_cast<int>(nl - pos);
                    if (w > max_w)
                        max_w = w;
                    pos = nl + 1;
                }
                widths[vi] = max_w + (vi == 0 ? 1 : 2);
            }
            tbl->access([&](const auto& rows) {
                for (const auto& row : rows) {
                    for (size_t vi = 0; vi < vis.size() && vis[vi] < row.cells.size(); ++vi) {
                        auto s = format_cell(cols[vis[vi]].type, row.cells[vis[vi]].data,
                                             cols[vis[vi]].decimals);
                        int  w = static_cast<int>(s.size()) + (vi == 0 ? 1 : 2);
                        if (w > widths[vi])
                            widths[vi] = w;
                    }
                }
            });

            // Right-aligned columns
            std::vector<bool> ralign(vis.size(), false);
            for (size_t vi = 0; vi < vis.size(); ++vi) {
                switch (cols[vis[vi]].type) {
                case ColumnType::Number:
                    ralign[vi] = true;
                    break;
                default:
                    break;
                }
            }

            // Header row (supports multi-line headers with \n, bottom-aligned)
            std::vector<std::vector<std::string>> hdr_lines(vis.size());
            size_t                                max_lines = 1;
            for (size_t vi = 0; vi < vis.size(); ++vi) {
                const std::string& hdr = cols[vis[vi]].header;
                size_t             pos = 0;
                while (pos <= hdr.size()) {
                    size_t nl = hdr.find('\n', pos);
                    if (nl == std::string::npos)
                        nl = hdr.size();
                    hdr_lines[vi].push_back(hdr.substr(pos, nl - pos));
                    pos = nl + 1;
                }
                if (hdr_lines[vi].size() > max_lines)
                    max_lines = hdr_lines[vi].size();
            }
            Elements hdr_cells;
            for (size_t vi = 0; vi < vis.size(); ++vi) {
                Elements lines;
                size_t   pad_lines = max_lines - hdr_lines[vi].size();
                for (size_t i = 0; i < pad_lines; ++i)
                    lines.push_back(text(""));
                std::string prefix = " ";
                for (const auto& line : hdr_lines[vi]) {
                    std::string s = line;
                    if (ralign[vi]) {
                        int pad = widths[vi] - static_cast<int>(s.size()) -
                                  static_cast<int>(prefix.size());
                        if (pad > 0)
                            s = std::string(pad, ' ') + s;
                    }
                    lines.push_back(text(prefix + s));
                }
                auto el = (max_lines == 1) ? std::move(lines[0]) : vbox(std::move(lines));
                if (vi + 1 < vis.size() || ralign[vi])
                    el = el | size(WIDTH, EQUAL, widths[vi]);
                else
                    el = el | flex;
                hdr_cells.push_back(std::move(el));
            }
            Elements tbl_rows;
            if (!tbl->no_header()) {
                tbl_rows.push_back(hbox(hdr_cells) | color(Color::Cyan) | bold);
                tbl_rows.push_back(separator());
            }

            // Data rows
            tbl->access([&](const auto& rows) {
                for (const auto& row : rows) {
                    Elements cells;
                    for (size_t vi = 0; vi < vis.size() && vis[vi] < row.cells.size(); ++vi) {
                        const auto& cv = row.cells[vis[vi]];
                        std::string val =
                            format_cell(cols[vis[vi]].type, cv.data, cols[vis[vi]].decimals);
                        std::string prefix = " ";
                        if (ralign[vi]) {
                            int pad = widths[vi] - static_cast<int>(val.size()) -
                                      static_cast<int>(prefix.size());
                            if (pad > 0)
                                val = std::string(pad, ' ') + val;
                        }
                        auto el = apply_style(text(prefix + val), cv);
                        if (vi + 1 < vis.size() || ralign[vi])
                            el = el | size(WIDTH, EQUAL, widths[vi]);
                        else
                            el = el | flex;
                        cells.push_back(el);
                    }
                    tbl_rows.push_back(hbox(cells));
                }
            });

            const auto& box_title = tbl->title();
            auto        hi        = tbl->header_info();
            auto        hi_str    = format_cell(ColumnType::String, hi.data);
            if (!hi_str.empty() && !box_title.empty()) {
                auto el = apply_style(text("  " + hi_str), hi);
                tbl_rows.insert(tbl_rows.begin(),
                                hbox({text(" " + box_title + " ") | bold | color(Color::Gold1),
                                      std::move(el) | flex}));
                lua_elems.push_back(vbox(std::move(tbl_rows)) | border);
            } else if (!box_title.empty()) {
                lua_elems.push_back(section_box(box_title, tbl_rows));
            } else {
                lua_elems.push_back(vbox(std::move(tbl_rows)) | border);
            }
        } else if (auto sum = std::dynamic_pointer_cast<LuaSummary>(panel)) {
            const auto& flds = sum->fields();
            Elements    rows;
            sum->access([&](const auto& values) {
                for (size_t i = 0; i < flds.size(); ++i) {
                    std::string label = "  " + flds[i].header + " : ";
                    std::string val   = format_cell(flds[i].type, values[i].data, flds[i].decimals);
                    auto        el    = apply_style(text(val), values[i]);
                    rows.push_back(hbox({text(label) | color(Color::GrayDark), std::move(el)}));
                }
            });
            const auto& box_title = sum->title();
            if (!box_title.empty()) {
                summary_run.push_back(section_box(box_title, rows) | flex);
            } else {
                summary_run.push_back(vbox(std::move(rows)) | border | flex);
            }
        }
    }
    flush_summaries();

    Elements panels;

    auto [init_err, errors, warnings] = lua_tab_state_.access(
        [](const auto& s) { return std::make_tuple(s.init_error, s.callback_errors, s.warnings); });
    if (init_err || !errors.empty() || !warnings.empty()) {
        auto format_entry = [](Elements& rows, const LuaError& err) {
            std::string prefix =
                " " + fmt_localtime(err.when, TimeFmt::HMS) + " " + err.source_id + ": ";
            if (err.message.find('\n') == std::string::npos) {
                rows.push_back(hbox({text(prefix) | bold, paragraph(err.message)}) |
                               color(Color::White));
            } else {
                rows.push_back(text(prefix) | bold | color(Color::White));
                std::istringstream ss(err.message);
                std::string        line;
                while (std::getline(ss, line)) {
                    rows.push_back(paragraph("    " + line) | color(Color::White));
                }
            }
        };
        Elements err_rows;
        err_rows.push_back(text(""));
        if (init_err) {
            err_rows.push_back(text(" Script initialization failed -- tab inactive") | bold |
                               color(Color::Yellow));
            err_rows.push_back(text(""));
        }
        if (init_err)
            format_entry(err_rows, *init_err);
        for (const auto& [id, err] : errors)
            format_entry(err_rows, err);
        for (const auto& w : warnings)
            format_entry(err_rows, w);
        panels.push_back(section_box("ERRORS", std::move(err_rows)) | color(Color::Red));
    }

    for (auto& lp : lua_elems) {
        panels.push_back(std::move(lp));
    }
    return vbox(panels) | flex;
}

void LuaTab::join() {
    if (lua_thread_.joinable())
        lua_thread_.join();
}
