#include "luatab.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <re2/re2.h>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include <LuaBridge/LuaBridge.h>
#include <LuaBridge/Vector.h>

#include "components/address.hpp"
#include "components/gauge.hpp"
#include "components/qr_item.hpp"
#include "components/qr_overlay.hpp"
#include "format.hpp"
#include "luatable.hpp"
#include "paths.hpp"
#include "render.hpp"

using namespace ftxui;
using Clock     = std::chrono::system_clock;
using TimePoint = Clock::time_point;

static const std::set<std::string> DEFAULT_RPC_ALLOWLIST = {
    "decoderawtransaction",
    "decodescript",
    "estimatesmartfee",
    "getbestblockhash",
    "getblock",
    "getblockchaininfo",
    "getblockcount",
    "getblockhash",
    "getblockheader",
    "getblockstats",
    "getchaintips",
    "getconnectioncount",
    "getdeploymentinfo",
    "getindexinfo",
    "getmempoolancestors",
    "getmempoolcluster",
    "getmempooldescendants",
    "getmempoolentry",
    "getmempoolfeeratediagram",
    "getmempoolinfo",
    "getmininginfo",
    "getnettotals",
    "getnetworkhashps",
    "getnetworkinfo",
    "getnodeaddresses",
    "getpeerinfo",
    "getrawmempool",
    "getrawtransaction",
    "gettxout",
    "gettxoutsetinfo",
    "logging",
    "uptime",
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
    int               id;
    RE2               pattern;
    int               ngroups;
    int64_t           backlog_bytes;
    luabridge::LuaRef callback;
    std::string       source_id;
    LogWatch(int id, const std::string& pat, luabridge::LuaRef fn, std::string src,
             int64_t backlog = 0)
        : id(id), pattern(pat), ngroups(pattern.NumberOfCapturingGroups()),
          backlog_bytes(std::max(int64_t{0}, backlog)), callback(std::move(fn)),
          source_id(std::move(src)) {}
};

struct TimerHandle {
    int id;
};

struct LuaTimer {
    int               id;
    Clock::duration   interval;
    luabridge::LuaRef callback;
    std::string       source_id;
};

// A timer callback that yielded an RPC and is waiting for its response. The Lua
// coroutine runs on its own `lua_State* co`, kept alive by a registry ref
// (luaL_ref) since LuaBridge has no coroutine wrapper — see resume_coro().
struct PendingCoroutine {
    lua_State* co         = nullptr;
    int        thread_ref = LUA_NOREF;
    LuaTimer   timer;
    bool       wake_pending = false;
};

} // namespace

struct LuaFooterBtn {
    int               id;
    std::string       label;
    std::string       key; // optional keyboard shortcut, e.g. "r"
    luabridge::LuaRef fn;
};

// ── Raw-C-API marshalling helpers ──────────────────────────────────────────
// LuaBridge handles function registration and value capture (LuaRef), but the
// JSON<->Lua conversion and coroutine plumbing use the raw C API directly.

namespace lb = luabridge;

// Read a string field from a Lua table ref, returning `def` when absent/non-string.
inline std::string field_or(const lb::LuaRef& t, const char* key, std::string def) {
    if (!t.isTable())
        return def;
    lb::LuaRef v = t[key];
    if (v.isString())
        return v.unsafe_cast<std::string>();
    return def;
}

inline bool field_or(const lb::LuaRef& t, const char* key, bool def) {
    if (!t.isTable())
        return def;
    lb::LuaRef v = t[key];
    if (v.isBool())
        return v.unsafe_cast<bool>();
    return def;
}

inline int field_or(const lb::LuaRef& t, const char* key, int def) {
    if (!t.isTable())
        return def;
    lb::LuaRef v = t[key];
    if (v.isNumber())
        return v.unsafe_cast<int>();
    return def;
}

// Push a json value onto L's stack as the equivalent Lua value.
static void push_json(lua_State* L, const json& j) {
    if (j.is_bool())
        lua_pushboolean(L, j.get<bool>());
    else if (j.is_number_integer())
        lua_pushinteger(L, static_cast<lua_Integer>(j.get<int64_t>()));
    else if (j.is_number_float())
        lua_pushnumber(L, j.get<double>());
    else if (j.is_string()) {
        auto s = j.get<std::string>();
        lua_pushlstring(L, s.data(), s.size());
    } else if (j.is_array()) {
        lua_createtable(L, static_cast<int>(j.size()), 0);
        for (size_t i = 0; i < j.size(); ++i) {
            push_json(L, j[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
        }
    } else if (j.is_object()) {
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for (auto& [k, v] : j.items()) {
            push_json(L, v);
            lua_setfield(L, -2, k.c_str());
        }
    } else {
        lua_pushnil(L);
    }
}

class LuaScript {
  public:
    LuaScript();
    ~LuaScript() {
        // Coroutines still suspended mid-RPC are abandoned at shutdown. Close them
        // with Lua 5.5's lua_closethread so their to-be-closed (`<close>`) variables
        // are finalized — plain GC at lua_close would not run those handlers.
        for (auto& [id, pc] : pending_) {
            if (pc.co)
                lua_closethread(pc.co, L_);
            if (pc.thread_ref != LUA_NOREF)
                luaL_unref(L_, LUA_REGISTRYINDEX, pc.thread_ref);
        }
        // Every LuaRef below anchors a value in the Lua registry and calls
        // luaL_unref on the state when destroyed. They must be released BEFORE
        // lua_close(L_), or that unref runs on a freed state (use-after-free).
        log_watches_.clear();
        timers_.clear();
        footer_btns_.clear();
        pending_.clear();
        on_resize_fn_.reset();
        input_confirm_fn_.reset();
        on_select_fn_.reset();
        if (L_)
            lua_close(L_);
    }
    LuaScript(const LuaScript&)            = delete;
    LuaScript& operator=(const LuaScript&) = delete;

    // Returns an error message on failure to load/run the script, or nullopt on success.
    std::optional<std::string> load(const std::string& script_path);

    lua_State*                              lua() { return L_; }
    std::vector<std::unique_ptr<LogWatch>>& log_watches() { return log_watches_; }
    std::map<TimePoint, LuaTimer>&          timers() { return timers_; }
    std::vector<LuaFooterBtn>&              footer_btns() { return footer_btns_; }

    void add_log_watch(const std::string& pattern, luabridge::LuaRef fn, std::string source_id,
                       int64_t backlog);
    TimerHandle add_timer(Clock::duration interval, luabridge::LuaRef fn, std::string source_id);
    int         add_footer_btn(std::string label, std::string key, luabridge::LuaRef fn);
    void        wake(const TimerHandle& h);
    std::map<int, PendingCoroutine>& pending() { return pending_; }

    static CellData  to_cell_data(ColumnType type, int decimals, const luabridge::LuaRef& v);
    static CellValue to_cell_value(ColumnType type, int decimals, const luabridge::LuaRef& v);
    static CellData  to_key(LuaTable& self, const luabridge::LuaRef& v);

    std::vector<LuaError>& warnings() { return warnings_; }

    void add_warning(std::string source_id, std::string msg) {
        warnings_.push_back({std::move(source_id), std::move(msg), Clock::now()});
    }

  private:
    lua_State*                             L_ = nullptr; // owned; closed in destructor
    std::vector<std::unique_ptr<LogWatch>> log_watches_;
    std::map<TimePoint, LuaTimer>          timers_;
    std::vector<LuaFooterBtn>              footer_btns_;
    int                                    next_callback_id_ = 0;
    std::map<int, PendingCoroutine>        pending_;
    std::vector<LuaError>                  warnings_;

  public:
    std::ostream*                    debug_out = nullptr;
    std::string                      on_resize_src_;
    std::optional<luabridge::LuaRef> on_resize_fn_;     // set by btcui_on_resize
    std::optional<luabridge::LuaRef> input_confirm_fn_; // set by btcui_text_input
    std::string                      on_select_src_;
    std::optional<luabridge::LuaRef> on_select_fn_; // set by btcui_on_select
};

LuaScript::LuaScript() {
    L_ = luaL_newstate();
    // Open only a restricted set of libs (no io/os/package) for sandboxing.
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},          {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},   {LUA_MATHLIBNAME, luaopen_math},
        {LUA_COLIBNAME, luaopen_coroutine},
    };
    for (const auto& lib : libs) {
        luaL_requiref(L_, lib.name, lib.func, 1);
        lua_pop(L_, 1);
    }
    // Let C++ exceptions thrown in bound functions surface as Lua errors
    // (our Lua is built in C/longjmp mode).
    luabridge::enableExceptions(L_);
    // btcui_rpc / btcui_rpc_wallet are intentional API globals — declare them with
    // Lua 5.5's `global` keyword rather than relying on global-by-default. Note that
    // any `global` declaration voids global-by-default for the rest of the chunk, so
    // the stdlib globals these bodies reference (coroutine, error) must be declared too.
    if (luaL_dostring(L_, R"(
        global coroutine, error
        global function btcui_rpc(method, ...)
            local result, err = coroutine.yield('rpc', method, {...})
            if err then error(err, 2) end
            return result
        end
        global function btcui_rpc_wallet(wallet, method, ...)
            local result, err = coroutine.yield('rpc_wallet', wallet, method, {...})
            if err then error(err, 2) end
            return result
        end
    )") != LUA_OK) {
        if (debug_out)
            *debug_out << "[lua bootstrap error] " << lua_tostring(L_, -1) << "\n" << std::flush;
        lua_pop(L_, 1);
    }
}

void LuaScript::add_log_watch(const std::string& pattern, luabridge::LuaRef fn,
                              std::string source_id, int64_t backlog) {
    int id = ++next_callback_id_;
    log_watches_.push_back(
        std::make_unique<LogWatch>(id, pattern, std::move(fn), std::move(source_id), backlog));
}

TimerHandle LuaScript::add_timer(Clock::duration interval, luabridge::LuaRef fn,
                                 std::string source_id) {
    int id = ++next_callback_id_;
    timers_.insert({Clock::now(), {id, interval, std::move(fn), std::move(source_id)}});
    return {id};
}

int LuaScript::add_footer_btn(std::string label, std::string key, luabridge::LuaRef fn) {
    int id = ++next_callback_id_;
    footer_btns_.push_back({id, std::move(label), std::move(key), std::move(fn)});
    return id;
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

std::optional<std::string> LuaScript::load(const std::string& script_path) {
    if (luaL_loadfile(L_, script_path.c_str()) != LUA_OK || lua_pcall(L_, 0, 0, 0) != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        std::string err = msg ? msg : "unknown error";
        lua_pop(L_, 1);
        return err;
    }
    return std::nullopt;
}

CellData LuaScript::to_cell_data(ColumnType type, int decimals, const lb::LuaRef& v) {
    switch (type) {
    case ColumnType::Number:
        if (decimals >= 0)
            return v.isNumber() ? v.unsafe_cast<double>() : 0.0;
        // Integer column: lua_tointeger preserves full 64-bit ints, truncates floats.
        return v.isNumber() ? CellData{v.unsafe_cast<int64_t>()} : CellData{int64_t(0)};
    case ColumnType::DateTime:
    case ColumnType::Date:
    case ColumnType::Time:
    case ColumnType::TimeMS:
        return v.isNumber() ? v.unsafe_cast<double>() : 0.0;
    default:
        if (v.isString())
            return v.unsafe_cast<std::string>();
        if (v.isNumber())
            return std::to_string(v.unsafe_cast<double>());
        return std::string{};
    }
}

CellValue LuaScript::to_cell_value(ColumnType type, int decimals, const lb::LuaRef& v) {
    CellValue cv;
    if (v.isTable()) {
        lb::LuaRef addr = v["__address"];
        if (addr.isString()) {
            cv.data = Address{addr.unsafe_cast<std::string>()};
            return cv;
        }
        lb::LuaRef gfrac = v["__gauge"];
        if (gfrac.isNumber()) {
            cv.color = field_or(v, "color", std::string{});
            cv.data  = Gauge{gfrac.unsafe_cast<double>(), field_or(v, "prefix", std::string{})};
            return cv;
        }
        cv.color      = field_or(v, "color", std::string{});
        cv.bold       = field_or(v, "bold", false);
        lb::LuaRef vv = v["value"];
        cv.data       = to_cell_data(type, decimals, vv);
    } else {
        cv.data = to_cell_data(type, decimals, v);
    }
    return cv;
}

CellData LuaScript::to_key(LuaTable& self, const lb::LuaRef& v) {
    return to_cell_data(self.key_type(), -1, v);
}

// ============================================================================
// Config file helpers (for btcui_config_path / btcui_config_read / btcui_config_write)
// ============================================================================

static std::string config_file_path() { return paths::config_file(); }

// Strip surrounding whitespace and quotes from a TOML scalar value
static std::string parse_toml_scalar(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    s = s.substr(a, b - a + 1);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

// Read config.toml into a Lua table {tabs, allow_rpc, refresh, host, port}
static lb::LuaRef read_config_table(lua_State* L) {
    lb::LuaRef t      = lb::newTable(L);
    lb::LuaRef tabs   = lb::newTable(L);
    lb::LuaRef rpcs   = lb::newTable(L);
    int        n_tabs = 0, n_rpc = 0;
    t["tabs"]        = tabs;
    t["allow_rpc"]   = rpcs;
    t["refresh"]     = 5;
    t["host"]        = std::string("127.0.0.1");
    t["port"]        = 8332;
    t["settingstab"] = true;
    t["exists"]      = false;

    std::ifstream f(config_file_path());
    if (!f)
        return t;

    t["exists"] = true;

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        {
            size_t ks = key.find_first_not_of(" \t");
            size_t ke = key.find_last_not_of(" \t");
            key       = (ks == std::string::npos) ? "" : key.substr(ks, ke - ks + 1);
        }
        std::string val = line.substr(eq + 1);
        {
            size_t vs = val.find_first_not_of(" \t");
            if (vs != std::string::npos)
                val = val.substr(vs);
        }

        enum class Key { Tab, AllowRpc, Refresh, Host, Port, SettingsTab, Unknown };
        auto classify = [](const std::string& k) {
            if (k == "tab")
                return Key::Tab;
            if (k == "allow-rpc")
                return Key::AllowRpc;
            if (k == "refresh")
                return Key::Refresh;
            if (k == "host")
                return Key::Host;
            if (k == "port")
                return Key::Port;
            if (k == "settingstab")
                return Key::SettingsTab;
            return Key::Unknown;
        };

        switch (classify(key)) {
        // `tab` and `allow-rpc` use repeated one-per-line entries (the legacy/CLI11
        // format). Accumulate into the existing table so multi-line configs are read
        // in full, not just the last occurrence.
        case Key::Tab: {
            auto sv = parse_toml_scalar(val);
            if (!sv.empty())
                tabs[++n_tabs] = sv;
            break;
        }
        case Key::AllowRpc: {
            auto sv = parse_toml_scalar(val);
            if (!sv.empty())
                rpcs[++n_rpc] = sv;
            break;
        }
        case Key::Refresh: {
            int v = 5;
            if (std::from_chars(val.data(), val.data() + val.size(), v).ec == std::errc{})
                t["refresh"] = v;
            break;
        }
        case Key::Host:
            t["host"] = parse_toml_scalar(val);
            break;
        case Key::Port: {
            int v = 8332;
            if (std::from_chars(val.data(), val.data() + val.size(), v).ec == std::errc{})
                t["port"] = v;
            break;
        }
        case Key::SettingsTab:
            t["settingstab"] = (parse_toml_scalar(val) == "true");
            break;
        case Key::Unknown:
            break;
        }
    }
    return t;
}

// Quote a string as a TOML basic string, escaping backslashes and quotes.
static std::string toml_quote(const std::string& v) {
    std::string s = "\"";
    for (char c : v) {
        if (c == '\\' || c == '"')
            s += '\\';
        s += c;
    }
    s += '"';
    return s;
}

// Write config.toml, preserving unknown keys and updating the managed keys
// (tab/allow-rpc/refresh/settingstab). Repeated entries for multi-value keys are
// collapsed at the first occurrence so no stale duplicates are left behind.
static bool write_config_table(const lb::LuaRef& cfg) {
    std::string path = config_file_path();
    if (path.empty())
        return false;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    // When run via sudo, hand the config dir back to the invoking user so it
    // stays usable without sudo.
    paths::chown_to_invoking_user(std::filesystem::path(path).parent_path().string());

    std::vector<std::string> existing;
    {
        std::ifstream f(path);
        std::string   line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            existing.push_back(line);
        }
    }

    const std::set<std::string> managed = {"tab", "allow-rpc", "refresh", "settingstab"};
    std::set<std::string>       written;

    // Returns the replacement line(s) for a managed key. Multi-value keys (tab,
    // allow-rpc) emit one `key = "value"` line per element — the format users
    // hand-write and the one CLI11 reads at startup.
    auto emit_key = [&](const std::string& key) -> std::vector<std::string> {
        std::vector<std::string> lines;
        enum class Key { Tab, AllowRpc, Refresh, SettingsTab, Other };
        auto classify = [](const std::string& k) {
            if (k == "tab")
                return Key::Tab;
            if (k == "allow-rpc")
                return Key::AllowRpc;
            if (k == "refresh")
                return Key::Refresh;
            if (k == "settingstab")
                return Key::SettingsTab;
            return Key::Other;
        };

        // Emit one `key = "value"` line per array element for multi-value keys.
        auto emit_array = [&](const char* field, const char* out_key) {
            lb::LuaRef arr = cfg[field];
            if (arr.isTable())
                for (int i = 1; i <= static_cast<int>(arr.length()); ++i) {
                    lb::LuaRef e = arr[i];
                    if (e.isString())
                        lines.push_back(std::string(out_key) + " = " +
                                        toml_quote(e.unsafe_cast<std::string>()));
                }
        };
        switch (classify(key)) {
        case Key::Tab:
            emit_array("tabs", "tab");
            break;
        case Key::AllowRpc:
            emit_array("allow_rpc", "allow-rpc");
            break;
        case Key::Refresh: {
            lb::LuaRef r = cfg["refresh"];
            if (r.isNumber())
                lines.push_back("refresh = " + std::to_string(r.unsafe_cast<int>()));
            break;
        }
        case Key::SettingsTab: {
            // Default is visible; only persist the non-default (false) so existing
            // "settingstab = true" lines are cleaned up when toggled back on.
            lb::LuaRef b = cfg["settingstab"];
            if (b.isBool() && !b.unsafe_cast<bool>())
                lines.push_back("settingstab = false");
            break;
        }
        case Key::Other:
            break;
        }
        return lines;
    };

    std::vector<std::string> out;
    for (const auto& line : existing) {
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            out.push_back(line);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            out.push_back(line);
            continue;
        }
        std::string key = line.substr(0, eq);
        {
            size_t ks = key.find_first_not_of(" \t");
            size_t ke = key.find_last_not_of(" \t");
            key       = (ks == std::string::npos) ? "" : key.substr(ks, ke - ks + 1);
        }
        if (managed.count(key)) {
            // Emit the full replacement set at the first occurrence; drop every
            // later occurrence so repeated `tab =` lines aren't duplicated.
            if (!written.count(key)) {
                written.insert(key);
                for (const auto& l : emit_key(key))
                    out.push_back(l);
            }
        } else {
            out.push_back(line);
        }
    }

    for (const auto& key : managed) {
        if (written.count(key))
            continue;
        for (const auto& l : emit_key(key))
            out.push_back(l);
    }

    std::ofstream f(path);
    if (!f)
        return false;
    for (const auto& line : out)
        f << line << "\n";
    f.close();
    paths::chown_to_invoking_user(path);
    return true;
}

void LuaTab::register_lua_api(LuaScript& script) {
    lua_State* L = script.lua();

    // Note: a trailing `lua_State*` parameter is filled with the calling state by
    // LuaBridge and not counted as a Lua argument (Stack<lua_State*> ignores its
    // index), so it must come last to keep the real args' stack positions aligned.
    luabridge::getGlobalNamespace(L).addFunction("btcui_error",
                                                 [&script](const std::string& msg, lua_State* L) {
                                                     script.add_warning(lua_source_id(L), msg);
                                                 });

    luabridge::getGlobalNamespace(L)
        .beginClass<LuaTable>("LuaTable")
        .addFunction("update",
                     [](LuaTable* self, const lb::LuaRef& key, const lb::LuaRef& data) {
                         std::map<std::string, CellValue> cells;
                         const auto&                      cols = self->columns();
                         for (lb::Iterator it(data); !it.isNil(); ++it) {
                             lb::LuaRef v = it.value();
                             if (v.isNil())
                                 continue;
                             std::string col_name = it.key().unsafe_cast<std::string>();
                             ColumnType  ct       = ColumnType::String;
                             int         dec      = -1;
                             for (const auto& col : cols) {
                                 if (col.name == col_name) {
                                     ct  = col.type;
                                     dec = col.decimals;
                                     break;
                                 }
                             }
                             cells[col_name] = LuaScript::to_cell_value(ct, dec, v);
                         }
                         self->update(LuaScript::to_key(*self, key), cells);
                     })
        .addFunction("remove",
                     [](LuaTable* self, const lb::LuaRef& key) {
                         return self->remove(LuaScript::to_key(*self, key));
                     })
        .addFunction("keys", &LuaTable::keys)
        .addFunction("start_refresh", &LuaTable::start_refresh)
        .addFunction("finish_refresh", &LuaTable::finish_refresh)
        .addFunction("set_header_info",
                     [](LuaTable* self, const lb::LuaRef& v) {
                         self->set_header_info(LuaScript::to_cell_value(ColumnType::String, -1, v));
                     })
        .addFunction("selected_key", &LuaTable::selected_key)
        .addFunction("selected_value", &LuaTable::selected_value)
        .endClass();

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_watch_log", [&script](const std::string& pattern, lb::LuaRef fn,
                                     std::optional<int64_t> backlog, lua_State* L) {
            script.add_log_watch(pattern, std::move(fn), lua_source_id(L), backlog.value_or(0));
        });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_table", [this](const lb::LuaRef& opts) -> std::shared_ptr<LuaTable> {
            lb::LuaRef             col_defs = opts["columns"];
            std::vector<ColumnDef> cols;
            for (int i = 1; i <= static_cast<int>(col_defs.length()); ++i) {
                lb::LuaRef  col      = col_defs[i];
                std::string name     = col["name"].unsafe_cast<std::string>();
                std::string header   = field_or(col, "header", name);
                std::string type_str = field_or(col, "type", std::string("string"));
                auto        type     = parse_column_type(type_str);
                if (!type) {
                    throw std::runtime_error("unknown column type: " + type_str);
                }
                int decimals = field_or(col, "decimals", -1);
                cols.push_back({std::move(name), std::move(header), *type, decimals});
            }
            std::string def_key    = cols.empty() ? std::string{} : cols[0].name;
            std::string key_column = field_or(opts, "key", std::move(def_key));
            std::string title      = field_or(opts, "title", std::string{});
            bool        no_header  = field_or(opts, "no_header", false);
            auto tbl = std::make_shared<LuaTable>(key_column, std::move(cols), std::move(title),
                                                  no_header);
            lua_tab_state_.update(
                [&](auto& st) { st.lua_panels.push_back(std::make_shared<LuaPanelRender>(tbl)); });
            return tbl;
        });

    luabridge::getGlobalNamespace(L)
        .beginClass<LuaSummary>("LuaSummary")
        .addFunction("set",
                     [](LuaSummary* self, const lb::LuaRef& data) {
                         std::map<std::string, CellValue> cells;
                         const auto&                      flds = self->fields();
                         for (lb::Iterator it(data); !it.isNil(); ++it) {
                             lb::LuaRef v = it.value();
                             if (v.isNil())
                                 continue;
                             std::string field_name = it.key().unsafe_cast<std::string>();
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
                         self->set(cells);
                     })
        .endClass();

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_summary", [this](const lb::LuaRef& opts) -> std::shared_ptr<LuaSummary> {
            lb::LuaRef             field_defs = opts["fields"];
            std::vector<ColumnDef> fields;
            size_t                 max_label = 0;
            for (int i = 1; i <= static_cast<int>(field_defs.length()); ++i) {
                lb::LuaRef  f        = field_defs[i];
                std::string name     = f["name"].unsafe_cast<std::string>();
                std::string label    = field_or(f, "label", name);
                std::string type_str = field_or(f, "type", std::string("string"));
                auto        type     = parse_column_type(type_str);
                if (!type) {
                    throw std::runtime_error("unknown field type: " + type_str);
                }
                if (label.size() > max_label)
                    max_label = label.size();
                int decimals = field_or(f, "decimals", -1);
                fields.push_back({std::move(name), std::move(label), *type, decimals});
            }
            // Pad labels so colons align in the rendered summary
            for (auto& f : fields)
                f.header.resize(max_label, ' ');

            std::string title   = field_or(opts, "title", std::string{});
            bool        new_row = field_or(opts, "new_row", false);
            auto sum = std::make_shared<LuaSummary>(std::move(fields), std::move(title), new_row);
            lua_tab_state_.update(
                [&](auto& st) { st.lua_panels.push_back(std::make_shared<LuaPanelRender>(sum)); });
            return sum;
        });

    luabridge::getGlobalNamespace(L).addFunction("btcui_key_hint", [this](std::string hint) {
        lua_tab_state_.update([&](auto& st) { st.lua_status = hint; });
    });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_add_footer_button", [&script, this](const std::string& label, lb::LuaRef fn,
                                                   const std::optional<std::string>& key) {
            std::string k = key.value_or("");
            if (k.empty()) {
                // Auto-extract key from "[x]" pattern in label
                auto lb = label.find('[');
                auto rb = label.find(']');
                if (lb != std::string::npos && rb == lb + 2)
                    k = label.substr(lb + 1, 1);
            }
            int id = script.add_footer_btn(label, k, std::move(fn));
            lua_tab_state_.update(
                [&](auto& st) { st.footer_btn_labels.push_back({id, label, k}); });
        });

    luabridge::getGlobalNamespace(L).addFunction("btcui_show_search_button", [this](bool show) {
        lua_tab_state_.update([show](auto& st) { st.show_search = show; });
    });

    luabridge::getGlobalNamespace(L).addFunction("btcui_show_quit_button", [this](bool show) {
        lua_tab_state_.update([show](auto& st) { st.show_quit = show; });
    });

    luabridge::getGlobalNamespace(L).beginClass<TimerHandle>("TimerHandle").endClass();

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_set_interval", [&script](double secs, lb::LuaRef fn, lua_State* L) -> TimerHandle {
            auto interval =
                std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(secs));
            return script.add_timer(interval, std::move(fn), lua_source_id(L));
        });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_wake", [&script](const TimerHandle& h) { script.wake(h); });

    luabridge::getGlobalNamespace(L).addFunction("btcui_set_name", [this](std::string name) {
        lua_tab_state_.update([&](auto& st) { st.tab_name = name; });
    });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_screen_size",
        [this]() -> std::tuple<int, int> { return {screen_.dimx(), screen_.dimy()}; });

    luabridge::getGlobalNamespace(L).addFunction("btcui_on_resize",
                                                 [&script](lb::LuaRef fn, lua_State* L) {
                                                     script.on_resize_src_ = lua_source_id(L);
                                                     script.on_resize_fn_  = std::move(fn);
                                                 });

    luabridge::getGlobalNamespace(L).addFunction("btcui_on_select",
                                                 [&script](lb::LuaRef fn, lua_State* L) {
                                                     script.on_select_src_ = lua_source_id(L);
                                                     script.on_select_fn_  = std::move(fn);
                                                 });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_option",
        [this](const std::string& key, std::optional<lb::LuaRef> default_val,
               lua_State* L) -> lb::LuaRef {
            if (tab_options_.contains(key))
                return lb::LuaRef(L, tab_options_[key].get<std::string>());
            if (default_val)
                return *default_val;
            throw std::runtime_error("required tab option '" + key + "' not set");
        });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_address", [](const std::string& addr, lua_State* L) -> lb::LuaRef {
            lb::LuaRef t   = lb::newTable(L);
            t["__address"] = addr;
            return t;
        });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_gauge", [](double frac, std::optional<lb::LuaRef> opts, lua_State* L) -> lb::LuaRef {
            lb::LuaRef t = lb::newTable(L);
            t["__gauge"] = frac;
            if (opts && opts->isTable()) {
                std::string prefix = field_or(*opts, "prefix", std::string{});
                if (!prefix.empty())
                    t["prefix"] = prefix;
                std::string color = field_or(*opts, "color", std::string{});
                if (!color.empty())
                    t["color"] = color;
            }
            return t;
        });

    luabridge::getGlobalNamespace(L).addFunction("btcui_script_dir", [this]() -> std::string {
        if (!tab_options_.contains("script"))
            return "";
        return std::filesystem::path(tab_options_["script"].get<std::string>())
            .parent_path()
            .string();
    });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_list_files", [](const std::string& dir, lua_State* L) -> lb::LuaRef {
            lb::LuaRef      result = lb::newTable(L);
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
                return result;
            std::vector<std::pair<std::string, std::string>> entries; // {name, path}
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.path().extension() == ".lua")
                    entries.emplace_back(entry.path().stem().string(), entry.path().string());
            }
            std::sort(entries.begin(), entries.end());
            int n = 0;
            for (auto& [name, path] : entries) {
                lb::LuaRef item = lb::newTable(L);
                item["name"]    = name;
                item["path"]    = path;
                result[++n]     = item;
            }
            return result;
        });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_config_path", []() -> std::string { return config_file_path(); });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_config_read", [](lua_State* L) -> lb::LuaRef { return read_config_table(L); });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_config_write",
        [](const lb::LuaRef& cfg) -> bool { return write_config_table(cfg); });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_text_input",
        [this, &script](std::string label, std::string default_val, lb::LuaRef on_confirm) {
            script.input_confirm_fn_ = std::move(on_confirm);
            lua_tab_state_.update([&](auto& st) {
                st.input_overlay.active = true;
                st.input_overlay.label  = label;
                st.input_overlay.buffer = default_val;
                st.input_overlay.cursor = static_cast<int>(default_val.size());
            });
            screen_.PostEvent(ftxui::Event::Custom);
        });

    luabridge::getGlobalNamespace(L).addFunction("btcui_reload_tabs", [this]() {
        if (reload_request_fn_)
            reload_request_fn_();
    });

    luabridge::getGlobalNamespace(L).addFunction(
        "btcui_open_qr_overlay", [this](const lb::LuaRef& arg) {
            QrItems items;
            if (arg.isString()) {
                items.push_back({"", arg.unsafe_cast<std::string>()});
            } else if (arg.isTable()) {
                for (int i = 1; i <= static_cast<int>(arg.length()); ++i) {
                    lb::LuaRef entry = arg[i];
                    if (entry.isString()) {
                        items.push_back({"", entry.unsafe_cast<std::string>()});
                    } else if (entry.isTable()) {
                        std::string label = field_or(entry, "label", std::string{});
                        std::string data  = field_or(entry, "data", std::string{});
                        items.push_back({std::move(label), std::move(data)});
                    }
                }
            }
            if (items.empty())
                return;
            lua_tab_state_.update([&](auto& st) {
                st.show_qr_overlay = true;
                st.qr_selected     = 0;
                st.qr_items        = std::move(items);
            });
        });
}

void LuaTab::rpc_thread_fn(WaitableGuarded<std::deque<RpcRequest>>&  requests,
                           WaitableGuarded<std::deque<RpcResponse>>& responses) {
    while (running_ && !stopped_) {
        auto req =
            requests.access_when([&](auto& q) { return !q.empty() || !running_ || stopped_; },
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

// Read an RPC params array (a Lua table at absolute stack index `idx` on `co`)
// into json, preserving the integer/float distinction via the Lua number subtype.
static json extract_rpc_params(lua_State* co, int idx) {
    std::vector<json> pv;
    int               n = static_cast<int>(lua_rawlen(co, idx));
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(co, idx, i);
        switch (lua_type(co, -1)) {
        case LUA_TBOOLEAN:
            pv.emplace_back(static_cast<bool>(lua_toboolean(co, -1)));
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(co, -1))
                pv.emplace_back(static_cast<int64_t>(lua_tointeger(co, -1)));
            else
                pv.emplace_back(static_cast<double>(lua_tonumber(co, -1)));
            break;
        case LUA_TSTRING:
            pv.emplace_back(std::string(lua_tostring(co, -1)));
            break;
        default:
            break;
        }
        lua_pop(co, 1);
    }
    return json(std::move(pv));
}

void LuaTab::lua_thread_fn(std::unique_ptr<LuaScript> script) {
    lua_State* lua         = script->lua();
    auto&      log_watches = script->log_watches();
    auto&      timers      = script->timers();

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

    // Drive a Lua coroutine `co`, which already has `nargs` arguments pushed onto
    // its stack. Resumes it and interprets each yield: a valid rpc/rpc_wallet yield
    // submits the request and returns its id (co stays suspended); when the
    // coroutine finishes (or errors), returns nullopt and reports against `timer`.
    auto drive = [&](lua_State* co, const LuaTimer& timer, int nargs) -> std::optional<int> {
        for (;;) {
            int nres   = 0;
            int status = lua_resume(co, lua, nargs, &nres);
            if (status == LUA_YIELD) {
                std::string tag =
                    (nres >= 1 && lua_type(co, 1) == LUA_TSTRING) ? lua_tostring(co, 1) : "";
                auto deny = [&](const std::string& method) {
                    lua_settop(co, 0);
                    lua_pushnil(co);
                    lua_pushstring(co, ("RPC method not allowed: " + method).c_str());
                    nargs = 2;
                };
                if (tag == "rpc" && nres >= 3) {
                    std::string method = lua_tostring(co, 2);
                    if (!rpc_allowlist_.contains(method)) {
                        deny(method);
                        continue;
                    }
                    json params = extract_rpc_params(co, 3);
                    lua_settop(co, 0);
                    return submit_rpc(method, std::move(params));
                }
                if (tag == "rpc_wallet" && nres >= 4) {
                    std::string wallet = lua_tostring(co, 2);
                    std::string method = lua_tostring(co, 3);
                    if (!rpc_allowlist_.contains(method)) {
                        deny(method);
                        continue;
                    }
                    json params = extract_rpc_params(co, 4);
                    lua_settop(co, 0);
                    return submit_rpc(method, std::move(params), wallet);
                }
                // Unknown yield — treat the coroutine as finished.
                lua_settop(co, 0);
                clear_callback_error(timer.id);
                return std::nullopt;
            }
            if (status != LUA_OK) {
                const char* msg = lua_tostring(co, -1);
                report_callback_error(timer.id, timer.source_id, msg ? msg : "error");
            } else {
                clear_callback_error(timer.id);
            }
            lua_settop(co, 0);
            return std::nullopt;
        }
    };

    std::string line;
    while (running_ && !stopped_) {
        // 0. Dispatch footer button clicks posted from the UI thread
        auto clicks = btn_click_queue_.update([](auto& q) { return std::exchange(q, {}); });
        if (debug_out_)
            *debug_out_ << "[ btn_click_queue ] " << clicks.size() << " clicks\n" << std::flush;
        for (int btn_id : clicks) {
            for (auto& b : script->footer_btns()) {
                if (b.id != btn_id) {
                    if (debug_out_) {
                        *debug_out_ << "[ btn_click_queue mismatch] " << b.id << ": " << btn_id
                                    << "\n"
                                    << std::flush;
                    }
                    continue;
                }
                if (debug_out_) {
                    *debug_out_ << "[ btn_click_queue match] " << b.id << ": " << btn_id << "\n"
                                << std::flush;
                }
                auto result = b.fn();
                if (!result) {
                    report_callback_error(btn_id, "footer_btn", result.message());
                } else {
                    if (debug_out_) {
                        *debug_out_ << "[ btn_click_queue success] " << b.id << ": " << btn_id
                                    << "\n"
                                    << std::flush;
                    }
                    clear_callback_error(btn_id);
                }
                break;
            }
        }

        // 0b. Fire resize callback if the UI reported a size change
        if (resize_pending_.exchange(false) && script->on_resize_fn_) {
            auto result = (*script->on_resize_fn_)(screen_.dimx(), screen_.dimy());
            if (!result) {
                report_callback_error(-1, script->on_resize_src_, result.message());
            } else {
                clear_callback_error(-1);
            }
        }

        // 0c. Dispatch text-input confirm/cancel results from the UI thread
        {
            auto results = input_result_queue_.update([](auto& q) { return std::exchange(q, {}); });
            for (auto& res : results) {
                if (script->input_confirm_fn_) {
                    luabridge::LuaRef fn = std::move(*script->input_confirm_fn_);
                    script->input_confirm_fn_.reset();
                    auto r = res.has_value() ? fn(*res) : fn(luabridge::LuaRef(lua));
                    if (!r) {
                        report_callback_error(-2, "text_input", r.message());
                    } else {
                        clear_callback_error(-2);
                    }
                }
            }
        }

        // 0d. Dispatch row-activation (Enter / mouse click) selected keys
        {
            auto keys = select_queue_.update([](auto& q) { return std::exchange(q, {}); });
            for (auto& key : keys) {
                if (script->on_select_fn_) {
                    auto r = (*script->on_select_fn_)(key);
                    if (!r) {
                        report_callback_error(-3, script->on_select_src_, r.message());
                    } else {
                        clear_callback_error(-3);
                    }
                }
            }
        }

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
                        // Variable arg count (ts, msg, captures...) → raw pcall.
                        lw->callback.push();
                        lua_pushnumber(lua, ts);
                        lua_pushlstring(lua, msg.data(), msg.size());
                        for (int i = 0; i < n; ++i)
                            lua_pushlstring(lua, captures[i].data(), captures[i].size());
                        if (lua_pcall(lua, 2 + n, 0, 0) != LUA_OK) {
                            const char* m = lua_tostring(lua, -1);
                            report_callback_error(lw->id, lw->source_id, m ? m : "error");
                            lua_pop(lua, 1);
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

            auto& pc = it->second;
            // Resume the coroutine with (value, err): push both onto its stack.
            lua_settop(pc.co, 0);
            if (resp.error.empty()) {
                push_json(pc.co, resp.result);
                lua_pushnil(pc.co);
            } else {
                lua_pushnil(pc.co);
                lua_pushlstring(pc.co, resp.error.data(), resp.error.size());
            }

            auto new_rpc_id = drive(pc.co, pc.timer, 2);
            if (new_rpc_id) {
                auto node  = pending.extract(it);
                node.key() = *new_rpc_id;
                pending.insert(std::move(node));
            } else {
                luaL_unref(lua, LUA_REGISTRYINDEX, pc.thread_ref);
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

            // New coroutine thread, anchored in the registry so it survives GC
            // while suspended. drive() runs it (no initial args).
            lua_State* co         = lua_newthread(lua);
            int        thread_ref = luaL_ref(lua, LUA_REGISTRYINDEX);
            timer.callback.push(); // push the callback onto `lua`
            lua_xmove(lua, co, 1); // move it onto the coroutine

            auto rpc_id = drive(co, timer, 0);
            if (rpc_id) {
                pending.emplace(*rpc_id, PendingCoroutine{co, thread_ref, std::move(timer), false});
                now = Clock::now();
                continue; // timer NOT rescheduled yet
            }
            // Finished synchronously — release the thread and reschedule.
            luaL_unref(lua, LUA_REGISTRYINDEX, thread_ref);
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

    // Shutdown: wake rpc thread so it sees !running_ (or stopped_)
    requests.notify();
    rpc_thread.join();
    thread_done_.store(true);
}

static std::set<std::string> make_allowlist(std::span<const std::string> extra) {
    auto allowlist = DEFAULT_RPC_ALLOWLIST;
    allowlist.insert(extra.begin(), extra.end());
    return allowlist;
}

LuaTab::LuaTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
               std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
               std::string debug_log_path, json tab_options,
               std::span<const std::string> extra_rpcs, std::ostream* debug_out)
    : Tab(std::move(cfg), auth, screen, running, state, refresh_secs, debug_out),
      debug_log_path_(std::move(debug_log_path)), tab_options_(std::move(tab_options)),
      rpc_allowlist_(make_allowlist(extra_rpcs)) {
    const std::string lua_script = tab_options_["script"].get<std::string>();
    auto              script     = std::make_unique<LuaScript>();
    script->debug_out            = debug_out_;
    lua_tab_state_.update(
        [&](auto& st) { st.tab_name = std::filesystem::path(lua_script).stem().string(); });
    register_lua_api(*script);
    if (auto err = script->load(lua_script)) {
        if (debug_out_)
            *debug_out_ << "[lua init error] " << lua_script << ": " << *err << "\n" << std::flush;
        lua_tab_state_.update(
            [&](auto& st) { st.init_error = LuaError{lua_script, *err, Clock::now()}; });
        return;
    }
    // btcui_set_name is only valid during script load; replace it afterwards.
    luabridge::getGlobalNamespace(script->lua())
        .addFunction("btcui_set_name", [](const std::string&) {
            throw std::runtime_error("btcui_set_name() can only be called during script loading");
        });
    lua_thread_ = std::thread(&LuaTab::lua_thread_fn, this, std::move(script));
}

std::string LuaTab::name() const {
    if (tab_options_.contains("t"))
        return tab_options_["t"].get<std::string>();
    return lua_tab_state_.access([](const auto& s) { return s.tab_name; });
}

std::string LuaTab::script_path() const {
    if (tab_options_.contains("script"))
        return tab_options_["script"].get<std::string>();
    return "";
}

void LuaTab::set_reload_callback(std::function<void()> fn) { reload_request_fn_ = std::move(fn); }

void LuaTab::stop() {
    stopped_.store(true);
    // The lua thread polls at a ≤1s cadence (responses.wait_until cap), so it
    // observes stopped_ and exits without needing an explicit wake.
}

bool LuaTab::finished() const { return thread_done_.load(); }

void LuaTab::report_callback_error(int id, const std::string& source_id, const std::string& msg) {
    if (debug_out_)
        *debug_out_ << "[lua error] " << source_id << ": " << msg << "\n" << std::flush;
    lua_tab_state_.update(
        [&](auto& st) { st.callback_errors[id] = {source_id, msg, Clock::now()}; });
}

void LuaTab::clear_callback_error(int id) {
    lua_tab_state_.update([&](auto& st) { st.callback_errors.erase(id); });
}

bool LuaTab::handle_focused_event(const Event& event) {
    // Text input overlay — must be checked before QR overlay and tab navigation
    if (lua_tab_state_.access([](const auto& s) { return s.input_overlay.active; })) {
        if (event == Event::Escape) {
            lua_tab_state_.update([](auto& s) {
                s.input_overlay.active = false;
                s.input_overlay.buffer.clear();
            });
            input_result_queue_.update([](auto& q) { q.push_back(std::nullopt); });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Return) {
            std::string val =
                lua_tab_state_.access([](const auto& s) { return s.input_overlay.buffer; });
            lua_tab_state_.update([](auto& s) {
                s.input_overlay.active = false;
                s.input_overlay.buffer.clear();
            });
            input_result_queue_.update([&val](auto& q) { q.push_back(std::move(val)); });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Backspace) {
            lua_tab_state_.update([](auto& s) {
                if (s.input_overlay.cursor > 0) {
                    s.input_overlay.buffer.erase(s.input_overlay.cursor - 1, 1);
                    --s.input_overlay.cursor;
                }
            });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowLeft) {
            lua_tab_state_.update([](auto& s) {
                if (s.input_overlay.cursor > 0)
                    --s.input_overlay.cursor;
            });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowRight) {
            lua_tab_state_.update([](auto& s) {
                if (s.input_overlay.cursor < static_cast<int>(s.input_overlay.buffer.size()))
                    ++s.input_overlay.cursor;
            });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Home) {
            lua_tab_state_.update([](auto& s) { s.input_overlay.cursor = 0; });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::End) {
            lua_tab_state_.update([](auto& s) {
                s.input_overlay.cursor = static_cast<int>(s.input_overlay.buffer.size());
            });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event.is_character()) {
            std::string ch = event.character();
            lua_tab_state_.update([&ch](auto& s) {
                s.input_overlay.buffer.insert(s.input_overlay.cursor, ch);
                s.input_overlay.cursor += static_cast<int>(ch.size());
            });
            screen_.PostEvent(Event::Custom);
            return true;
        }
        return true; // consume all events while input overlay is active
    }

    if (lua_tab_state_.access([](const auto& s) { return s.show_qr_overlay; })) {
        if (event == Event::Escape) {
            lua_tab_state_.update([](auto& s) {
                s.show_qr_overlay = false;
                s.qr_selected     = 0;
                s.qr_items.clear();
            });
            screen_.PostEvent(Event::Custom);
        } else if (event == Event::ArrowLeft) {
            lua_tab_state_.update([](auto& s) {
                if (s.qr_selected > 0)
                    --s.qr_selected;
            });
            screen_.PostEvent(Event::Custom);
        } else if (event == Event::ArrowRight) {
            lua_tab_state_.update([](auto& s) {
                if (s.qr_selected < static_cast<int>(s.qr_items.size()) - 1)
                    ++s.qr_selected;
            });
            screen_.PostEvent(Event::Custom);
        }
        return true;
    }

    if (event.is_character()) {
        auto btns = lua_tab_state_.access([](const auto& s) { return s.footer_btn_labels; });
        for (const auto& info : btns) {
            if (!info.key.empty() && event.character() == info.key) {
                int btn_id = info.id;
                btn_click_queue_.update([btn_id](auto& q) { q.push_back(btn_id); });
                return true;
            }
        }
    }

    // Mouse: a left-click selects a table row; a second click on the already-
    // selected row activates it (fires btcui_on_select) — mirroring the keyboard
    // flow where the first Enter selects and the second activates.
    if (event.is_mouse()) {
        auto& me = const_cast<Event&>(event).mouse();
        if (me.button == Mouse::Left && me.motion == Mouse::Pressed) {
            auto mpanels = lua_tab_state_.access([](const auto& s) { return s.lua_panels; });
            for (int pi = 0; pi < static_cast<int>(mpanels.size()); ++pi) {
                auto tbl = std::dynamic_pointer_cast<LuaTable>(mpanels[pi]->panel);
                if (!tbl)
                    continue;
                int row_idx = mpanels[pi]->row_hits.hit(me.x, me.y);
                if (row_idx < 0)
                    continue;
                bool already_selected = (focused_panel_.load() == pi && panel_scrolling_.load() &&
                                         tbl->selected_row().load() == row_idx);
                focused_panel_        = pi;
                panel_scrolling_      = true;
                tbl->selected_row()   = row_idx;
                if (already_selected) {
                    if (auto key = tbl->selected_key())
                        select_queue_.update([&](auto& q) { q.push_back(*key); });
                }
                screen_.PostEvent(Event::Custom);
                return true;
            }
        }
        return false;
    }

    auto panels = lua_tab_state_.access([](const auto& s) { return s.lua_panels; });
    int  n      = static_cast<int>(panels.size());
    if (n == 0)
        return false;

    int  fp        = focused_panel_.load();
    bool scrolling = panel_scrolling_.load();

    // Tables are always selectable; other panels only when scrollable.
    auto next_selectable = [&](int from, int dir) -> int {
        for (int i = from + dir; i >= 0 && i < n; i += dir) {
            if (std::dynamic_pointer_cast<LuaTable>(panels[i]->panel) || panels[i]->scrollable)
                return i;
        }
        return -1;
    };

    if (fp >= 0) {
        if (event == Event::Return) {
            // Already selecting a row on a table → Enter activates it (fires
            // btcui_on_select) and keeps selection mode, so the user can act
            // on several rows in a row.
            if (scrolling && fp < n) {
                if (auto tbl = std::dynamic_pointer_cast<LuaTable>(panels[fp]->panel)) {
                    if (auto key = tbl->selected_key()) {
                        select_queue_.update([&](auto& q) { q.push_back(*key); });
                        screen_.PostEvent(Event::Custom);
                        return true;
                    }
                }
            }
            bool entering    = !scrolling;
            panel_scrolling_ = entering;
            // When entering row-selection mode on a table, start at row 0.
            if (entering && fp < n) {
                if (auto tbl = std::dynamic_pointer_cast<LuaTable>(panels[fp]->panel))
                    if (tbl->selected_row().load() < 0)
                        tbl->selected_row() = 0;
            }
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (scrolling && fp < n) {
            auto& panel_render = panels[fp];
            if (event == Event::Escape) {
                panel_scrolling_ = false;
                if (auto tbl = std::dynamic_pointer_cast<LuaTable>(panel_render->panel))
                    tbl->selected_row() = -1;
                screen_.PostEvent(Event::Custom);
                return true;
            }
            if (auto tbl = std::dynamic_pointer_cast<LuaTable>(panel_render->panel)) {
                int count =
                    static_cast<int>(tbl->access([](const auto& rows) { return rows.size(); }));
                if (event == Event::ArrowDown) {
                    int cur = tbl->selected_row().load();
                    if (cur < count - 1)
                        tbl->selected_row() = cur + 1;
                    screen_.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowUp) {
                    int cur = tbl->selected_row().load();
                    if (cur > 0)
                        tbl->selected_row() = cur - 1;
                    screen_.PostEvent(Event::Custom);
                    return true;
                }
            } else {
                if (event == Event::ArrowDown) {
                    ++panel_render->scroll_offset;
                    screen_.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowUp) {
                    if (panel_render->scroll_offset > 0)
                        --panel_render->scroll_offset;
                    screen_.PostEvent(Event::Custom);
                    return true;
                }
            }
            return false;
        }
        if (event == Event::ArrowDown) {
            int next = next_selectable(fp, 1);
            if (next >= 0)
                focused_panel_ = next;
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowUp) {
            int prev = next_selectable(fp, -1);
            if (prev >= 0) {
                focused_panel_ = prev;
            } else {
                focused_panel_   = -1;
                panel_scrolling_ = false;
            }
            screen_.PostEvent(Event::Custom);
            return true;
        }
        return false;
    }

    // No panel highlighted — arrow keys enter panel-selection mode
    if (event == Event::ArrowDown) {
        int first = next_selectable(-1, 1);
        if (first >= 0)
            focused_panel_ = first;
        else
            return false;
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::ArrowUp) {
        int last = next_selectable(n, -1);
        if (last >= 0)
            focused_panel_ = last;
        else
            return false;
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

FooterSpec LuaTab::footer_buttons(const AppState& snap) {
    if (lua_tab_state_.access([](const auto& s) { return s.input_overlay.active; })) {
        return FooterSpec{
            {{{"[Enter] Confirm",
               [this] {
                   std::string val =
                       lua_tab_state_.access([](const auto& s) { return s.input_overlay.buffer; });
                   lua_tab_state_.update([](auto& s) {
                       s.input_overlay.active = false;
                       s.input_overlay.buffer.clear();
                   });
                   input_result_queue_.update([&val](auto& q) { q.push_back(std::move(val)); });
                   screen_.PostEvent(ftxui::Event::Custom);
               }},
              {"[Esc] Cancel",
               [this] {
                   lua_tab_state_.update([](auto& s) {
                       s.input_overlay.active = false;
                       s.input_overlay.buffer.clear();
                   });
                   input_result_queue_.update([](auto& q) { q.push_back(std::nullopt); });
                   screen_.PostEvent(ftxui::Event::Custom);
               }}}},
            false,
            false};
    }

    if (lua_tab_state_.access([](const auto& s) { return s.show_qr_overlay; })) {
        return FooterSpec{{{"[Esc] Close",
                            [this] {
                                lua_tab_state_.update([](auto& s) {
                                    s.show_qr_overlay = false;
                                    s.qr_selected     = 0;
                                    s.qr_items.clear();
                                });
                                screen_.PostEvent(Event::Custom);
                            }}},
                          false,
                          false};
    }

    struct Snapshot {
        std::string                             lua_status;
        std::vector<LuaTabState::FooterBtnInfo> footer_btn_labels;
        bool                                    show_search;
        bool                                    show_quit;
    };
    auto                      st = lua_tab_state_.access([](const auto& s) {
        return Snapshot{s.lua_status, s.footer_btn_labels, s.show_search, s.show_quit};
    });
    std::vector<FooterButton> btns;
    if (!st.lua_status.empty())
        btns.push_back({"  " + st.lua_status, nullptr, false});
    btns.push_back(refresh_btn(snap));
    for (const auto& info : st.footer_btn_labels) {
        int btn_id = info.id;
        btns.push_back({info.label, [this, btn_id] {
                            btn_click_queue_.update([btn_id](auto& q) { q.push_back(btn_id); });
                        }});
    }
    return FooterSpec{std::move(btns), st.show_search, st.show_quit};
}

// Map a Lua color name to an FTXUI color, falling back when unset/unknown.
static Color color_from_name(const std::string& name, Color fallback) {
    if (name == "red")
        return Color::Red;
    if (name == "green")
        return Color::Green;
    if (name == "yellow")
        return Color::Yellow;
    if (name == "cyan")
        return Color::Cyan;
    if (name == "gray")
        return Color::GrayDark;
    return fallback;
}

static Element apply_style(Element el, const CellValue& cv) {
    if (!cv.color.empty())
        el = el | color(color_from_name(cv.color, Color::Default));
    if (cv.bold)
        el = el | ftxui::bold;
    return el;
}

// Renders a cell value as an Element. Address cells use address_element();
// Gauge cells use gauge_element(); everything else is formatted text.
static Element render_cell_element(const std::string& prefix, const CellValue& cv, ColumnType type,
                                   int decimals) {
    if (std::holds_alternative<Address>(cv.data)) {
        auto el = address_element(std::get<Address>(cv.data).value);
        if (!prefix.empty())
            el = hbox({text(prefix), std::move(el)});
        return apply_style(std::move(el), cv);
    }
    if (std::holds_alternative<Gauge>(cv.data)) {
        const auto& g = std::get<Gauge>(cv.data);
        return gauge_element(g.frac, color_from_name(cv.color, Color::Cyan), g.prefix);
    }
    return apply_style(text(prefix + format_cell(type, cv.data, decimals)), cv);
}

Element LuaTab::render(const AppState& /*snap*/) {
    int dx     = screen_.dimx();
    int dy     = screen_.dimy();
    int prev_x = last_dimx_.exchange(dx);
    int prev_y = last_dimy_.exchange(dy);
    if ((prev_x != 0 || prev_y != 0) && (prev_x != dx || prev_y != dy))
        resize_pending_.store(true);

    struct PanelInfo {
        Elements chrome;             // title, header+separator (rendered before data rows)
        Elements data_rows;          // scrollable content
        int      chrome_height = 0;  // border(2) + chrome elements
        int      panel_index   = -1; // index into panels_vec, -1 for summary groups
    };
    std::vector<PanelInfo> lua_elems;
    LuaPanelVec panels_vec = lua_tab_state_.access([](const auto& s) { return s.lua_panels; });

    // Collect runs of consecutive summaries for side-by-side layout
    Elements summary_run;
    int      summary_max_height = 0;
    auto     flush_summaries    = [&]() {
        if (summary_run.empty())
            return;
        // Summaries are not scrollable — put everything in chrome, no data_rows
        lua_elems.push_back({{hbox(std::move(summary_run))}, {}, summary_max_height});
        summary_run.clear();
        summary_max_height = 0;
    };

    if (lua_tab_state_.access([](const auto& s) { return s.show_qr_overlay; })) {
        auto [items, sel] = lua_tab_state_.access(
            [](const auto& s) { return std::make_pair(s.qr_items, s.qr_selected); });
        return dbox(qr_overlay_element(items, sel));
    }

    if (lua_tab_state_.access([](const auto& s) { return s.input_overlay.active; })) {
        struct IOSnap {
            std::string label;
            std::string buffer;
            int         cursor;
        };
        auto io    = lua_tab_state_.access([](const auto& s) {
            return IOSnap{s.input_overlay.label, s.input_overlay.buffer, s.input_overlay.cursor};
        });
        int  width = std::max(52, static_cast<int>(io.label.size()) + 6);

        std::string before = io.buffer.substr(0, io.cursor);
        std::string at     = io.cursor < static_cast<int>(io.buffer.size())
                                 ? io.buffer.substr(io.cursor, 1)
                                 : std::string(" ");
        std::string after =
            io.cursor < static_cast<int>(io.buffer.size()) ? io.buffer.substr(io.cursor + 1) : "";

        Elements dialog_rows;
        dialog_rows.push_back(hbox({text(" "), text(io.label) | color(Color::White), filler()}));
        dialog_rows.push_back(separator());
        dialog_rows.push_back(
            hbox({text(" "), text(before), text(at) | inverted, text(after), filler()}) |
            color(Color::White));

        return center_overlay(build_titled_panel(" Input ", "", std::move(dialog_rows), width));
    }

    for (int pi = 0; pi < static_cast<int>(panels_vec.size()); ++pi) {
        const auto& panel_render = panels_vec[pi];
        const auto& panel        = panel_render->panel;
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
            Elements chrome;
            if (!tbl->no_header()) {
                chrome.push_back(hbox(hdr_cells) | color(Color::Cyan) | bold);
                chrome.push_back(separator());
            }

            const auto& box_title = tbl->title();
            auto        hi        = tbl->header_info();
            auto        hi_str    = format_cell(ColumnType::String, hi.data);
            if (!hi_str.empty() && !box_title.empty()) {
                auto el = apply_style(text("  " + hi_str), hi);
                chrome.insert(chrome.begin(),
                              hbox({text(" " + box_title + " ") | bold | color(Color::Gold1),
                                    std::move(el) | flex}));
            } else if (!box_title.empty()) {
                chrome.insert(chrome.begin(),
                              text(" " + box_title + " ") | bold | color(Color::Gold1));
            }

            // chrome_height: border(2) + chrome element count
            int chrome_h = 2 + static_cast<int>(chrome.size());

            // Data rows
            int      sel_row = tbl->selected_row().load();
            Elements data_rows;
            tbl->access([&](const auto& rows) {
                int row_idx = 0;
                for (const auto& row : rows) {
                    bool     is_selected = (row_idx == sel_row);
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
                                prefix += std::string(pad, ' ');
                        }
                        auto el = render_cell_element(prefix, cv, cols[vis[vi]].type,
                                                      cols[vis[vi]].decimals);
                        if (vi + 1 < vis.size() || ralign[vi])
                            el = el | size(WIDTH, EQUAL, widths[vi]);
                        else
                            el = el | flex;
                        cells.push_back(el);
                    }
                    auto row_el = hbox(cells);
                    if (is_selected)
                        row_el = row_el | inverted;
                    data_rows.push_back(std::move(row_el));
                    ++row_idx;
                }
            });

            lua_elems.push_back({std::move(chrome), std::move(data_rows), chrome_h, pi});
        } else if (auto sum = std::dynamic_pointer_cast<LuaSummary>(panel)) {
            if (sum->new_row())
                flush_summaries();
            const auto& flds = sum->fields();
            Elements    rows;
            sum->access([&](const auto& values) {
                for (size_t i = 0; i < flds.size(); ++i) {
                    std::string label = "  " + flds[i].header + " : ";
                    auto el = render_cell_element("", values[i], flds[i].type, flds[i].decimals);
                    rows.push_back(hbox({text(label) | color(Color::GrayDark), std::move(el)}));
                }
            });
            // Natural height: border(2) + title(0-1) + field rows
            const auto& box_title = sum->title();
            int         nat_h     = 2 + static_cast<int>(flds.size());
            if (!box_title.empty()) {
                ++nat_h;
                summary_run.push_back(section_box(box_title, rows) | flex);
            } else {
                summary_run.push_back(vbox(std::move(rows)) | border | flex);
            }
            if (nat_h > summary_max_height)
                summary_max_height = nat_h;
        }
    }
    flush_summaries();

    // Error panel (not subject to fair allocation — always shown in full)
    Element error_panel;
    int     error_height              = 0;
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
        error_height = static_cast<int>(err_rows.size()) + 3; // border(2) + title(1)
        error_panel  = section_box("ERRORS", std::move(err_rows)) | color(Color::Red);
    }

    // Fair-share height allocation
    // Outer chrome: title bar(3) + tab bar(3) + status bar(3) = 9
    int available = screen_.dimy() - 9 - error_height;
    int n         = static_cast<int>(lua_elems.size());

    // Natural height = chrome_height + data_rows.size()
    // (for summary groups, data_rows is empty and chrome_height is the full height)
    auto nat_height = [](const PanelInfo& p) {
        return p.chrome_height + static_cast<int>(p.data_rows.size());
    };

    std::vector<int> allocated(n);
    if (n > 0 && available > 0) {
        std::vector<bool> settled(n, false);
        int               remaining = available;
        int               unsettled = n;
        while (unsettled > 0) {
            int share    = remaining / unsettled;
            int progress = 0;
            for (int i = 0; i < n; ++i) {
                if (settled[i])
                    continue;
                if (nat_height(lua_elems[i]) <= share) {
                    allocated[i] = nat_height(lua_elems[i]);
                    remaining -= allocated[i];
                    settled[i] = true;
                    ++progress;
                }
            }
            unsettled -= progress;
            if (progress == 0) {
                for (int i = 0; i < n; ++i) {
                    if (!settled[i]) {
                        allocated[i] = remaining / unsettled;
                        remaining -= allocated[i];
                        --unsettled;
                    }
                }
                break;
            }
        }
    }

    int  fp        = focused_panel_.load();
    bool scrolling = panel_scrolling_.load();

    Elements panels;
    if (error_panel)
        panels.push_back(std::move(error_panel));
    for (int i = 0; i < n; ++i) {
        auto& lp = lua_elems[i];

        if (lp.data_rows.empty()) {
            // Summary group, or a table with no rows — clear any stale hitboxes so
            // a mouse click can't land on a row that's no longer there.
            if (lp.panel_index >= 0 && lp.panel_index < static_cast<int>(panels_vec.size()))
                panels_vec[lp.panel_index]->row_hits.clear();
            Elements content(std::move(lp.chrome));
            auto     elem = vbox(std::move(content));
            if (allocated[i] < nat_height(lp))
                elem = elem | size(HEIGHT, EQUAL, allocated[i]);
            panels.push_back(std::move(elem));
            continue;
        }

        // Table panel — apply scroll offset and row limit
        int max_data_rows = allocated[i] - lp.chrome_height;
        int total_rows    = static_cast<int>(lp.data_rows.size());
        int offset        = 0;

        if (lp.panel_index >= 0 && lp.panel_index < static_cast<int>(panels_vec.size())) {
            offset = panels_vec[lp.panel_index]->scroll_offset;
        }

        // Follow selected row: keep it within the visible window
        if (lp.panel_index >= 0 && lp.panel_index < static_cast<int>(panels_vec.size())) {
            if (auto tbl = std::dynamic_pointer_cast<LuaTable>(panels_vec[lp.panel_index]->panel)) {
                int sel = tbl->selected_row().load();
                if (sel >= 0 && max_data_rows > 0) {
                    if (sel < offset)
                        offset = sel;
                    else if (sel >= offset + max_data_rows)
                        offset = sel - max_data_rows + 1;
                }
            }
        }
        // Clamp scroll offset
        if (max_data_rows < total_rows) {
            offset = std::clamp(offset, 0, total_rows - max_data_rows);
        } else {
            offset = 0;
        }
        // Write back scrollability and clamped offset
        if (lp.panel_index >= 0 && lp.panel_index < static_cast<int>(panels_vec.size())) {
            bool is_scrollable                        = (total_rows > max_data_rows);
            panels_vec[lp.panel_index]->scrollable    = is_scrollable;
            panels_vec[lp.panel_index]->scroll_offset = is_scrollable ? offset : 0;
        }

        Elements content(std::move(lp.chrome));
        int      end = std::min(offset + max_data_rows, total_rows);
        // Capture per-row screen rectangles so the mouse can hit-test rows.
        LuaPanelRender* pr =
            (lp.panel_index >= 0 && lp.panel_index < static_cast<int>(panels_vec.size()))
                ? panels_vec[lp.panel_index].get()
                : nullptr;
        if (pr)
            pr->row_hits.clear();
        for (int r = offset; r < end; ++r) {
            if (pr)
                content.push_back(pr->row_hits.track(std::move(lp.data_rows[r]), r));
            else
                content.push_back(std::move(lp.data_rows[r]));
        }

        bool is_focused = (lp.panel_index >= 0 && lp.panel_index == fp);
        auto body       = vbox(std::move(content));
        if (is_focused && scrolling)
            panels.push_back(window(text("***") | bold | color(Color::Cyan), std::move(body)));
        else if (is_focused)
            panels.push_back(window(text("***") | color(Color::White), std::move(body)));
        else
            panels.push_back(std::move(body) | border);
    }
    return vbox(panels) | flex;
}

void LuaTab::join() {
    if (lua_thread_.joinable())
        lua_thread_.join();
}
