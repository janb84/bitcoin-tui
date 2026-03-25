#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "bitcoind.hpp"
#include "format.hpp"
#include "poll.hpp"
#include "render.hpp"
#include "rpc_client.hpp"
#include "search.hpp"
#include "state.hpp"

using namespace ftxui;

// ============================================================================
// Mutex helper
// ============================================================================

template <typename T> class Guarded {
  private:
    mutable std::mutex mtx;
    T                  value;

  public:
    Guarded()            = default;
    Guarded(Guarded&& v) = default;
    ~Guarded()           = default;

    explicit Guarded(T v) : value{std::move(v)} {}

    T get() const {
        std::lock_guard lock(mtx);
        return value;
    }

    operator T() const { return get(); }

    template <typename Fn> auto update(Fn&& fn) {
        std::lock_guard lock(mtx);
        return fn(value);
    }
};

// ============================================================================
// Cookie authentication helpers
// ============================================================================

// Returns the platform-specific default Bitcoin data directory.
static std::string default_datadir() {
    const char* home = std::getenv("HOME");
    if (!home)
        throw std::runtime_error("HOME not set; use --datadir or --cookie to locate .cookie");
#ifdef __APPLE__
    return std::string(home) + "/Library/Application Support/Bitcoin";
#else
    return std::string(home) + "/.bitcoin";
#endif
}

// Returns the chain subdirectory name for the given network.
static std::string network_subdir(const std::string& network) {
    if (network == "testnet3")
        return "testnet3/";
    if (network == "signet")
        return "signet/";
    if (network == "regtest")
        return "regtest/";
    return "";
}

// Returns the path to the .cookie file for the given network and data directory.
static std::string cookie_path(const std::string& network, const std::string& datadir) {
    return datadir + "/" + network_subdir(network) + ".cookie";
}

// Reads a Bitcoin Core cookie file and populates auth.user / auth.password.
// File format: __cookie__:<password>
static void apply_cookie(RpcAuth& auth, const std::string& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open cookie file: " + path);
    std::string line;
    if (!std::getline(f, line) || line.empty())
        throw std::runtime_error("Cookie file is empty: " + path);
    // Strip trailing \r in case the file has CRLF line endings
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    auto colon = line.find(':');
    if (colon == std::string::npos)
        throw std::runtime_error("Invalid cookie file (no ':' found): " + path);
    auth.user     = line.substr(0, colon);
    auth.password = line.substr(colon + 1);
}

// ============================================================================
// Application entry point (split so main() itself is exception-free)
// ============================================================================
static int run(int argc, char* argv[]) {
    // Parse CLI args
    RpcConfig        cfg;
    Guarded<RpcAuth> auth;
    int              refresh_secs = 5;
    std::string      network      = "main";  // tracks chain for cookie path lookup
    std::string      cookie_file;            // explicit --cookie override
    std::string      datadir;                // explicit --datadir override
    bool             explicit_creds = false; // true when -u/-P were given
    std::string      bitcoind_cmd;           // --bitcoind override
    bool             explicit_host = false;  // true when --host was given

    for (int i = 1; i < argc; ++i) {
        std::string arg  = argv[i];
        auto        next = [&]() -> std::string {
            if (i + 1 < argc)
                return argv[++i];
            return {};
        };
        if (arg == "--host" || arg == "-h") {
            cfg.host      = next();
            explicit_host = true;
        } else if (arg == "--port" || arg == "-p")
            cfg.port = std::stoi(next());
        else if (arg == "--user" || arg == "-u") {
            auth.update([&](auto& a) { a.user = next(); });
            explicit_creds = true;
        } else if (arg == "--password" || arg == "-P") {
            auth.update([&](auto& a) { a.password = next(); });
            explicit_creds = true;
        } else if (arg == "--cookie" || arg == "-c")
            cookie_file = next();
        else if (arg == "--datadir" || arg == "-d")
            datadir = next();
        else if (arg == "--refresh" || arg == "-r")
            refresh_secs = std::stoi(next());
        else if (arg == "--testnet") {
            cfg.port = 18332;
            network  = "testnet3";
        } else if (arg == "--regtest") {
            cfg.port = 18443;
            network  = "regtest";
        } else if (arg == "--signet") {
            cfg.port = 38332;
            network  = "signet";
        } else if (arg == "--bitcoind")
            bitcoind_cmd = next();
        else if (arg == "--version" || arg == "-v") {
            std::puts("bitcoin-tui " BITCOIN_TUI_VERSION);
            return 0;
        } else if (arg == "--help") {
            // clang-format off
            std::puts(
                "bitcoin-tui — Terminal UI for Bitcoin Core\n"
                "\n"
                "Usage: bitcoin-tui [options]\n"
                "\n"
                "Connection:\n"
                "  -h, --host <host>      RPC host             (default: 127.0.0.1)\n"
                "  -p, --port <port>      RPC port             (default: 8332)\n"
                "\n"
                "Authentication (cookie auth is used by default):\n"
                "  -c, --cookie <path>    Path to .cookie file (auto-detected if omitted)\n"
                "  -d, --datadir <path>   Bitcoin data directory for cookie lookup\n"
                "  -u, --user <user>      RPC username         (disables cookie auth)\n"
                "  -P, --password <pass>  RPC password         (disables cookie auth)\n"
                "\n"
                "Node:\n"
                "      --bitcoind <path>  Path to bitcoind binary   (default: bitcoind from PATH)\n"
                "\n"
                "Network:\n"
                "      --testnet          Use testnet3 port (18332) and cookie subdir\n"
                "      --regtest          Use regtest  port (18443) and cookie subdir\n"
                "      --signet           Use signet   port (38332) and cookie subdir\n"
                "\n"
                "Display:\n"
                "  -r, --refresh <secs>   Refresh interval     (default: 5)\n"
                "  -v, --version          Print version and exit\n"
                "\n"
                "Keyboard:\n"
                "  Tab / Left / Right     Switch tabs\n"
                "  /                      Activate txid search\n"
                "  Enter                  Submit search\n"
                "  Escape                 Cancel input / dismiss result / quit\n"
                "  q                      Quit\n"
            );
            // clang-format on
            return 0;
        }
    }

    // Resolve data directory (needed for cookie path and overlay display).
    if (datadir.empty())
        datadir = default_datadir();

    // Apply cookie authentication unless explicit -u/-P credentials were given.
    if (!explicit_creds) {
        std::string path = cookie_file.empty() ? cookie_path(network, datadir) : cookie_file;
        try {
            auth.update([&](auto& a) { apply_cookie(a, path); });
        } catch (const std::exception& e) {
            // If the user specified --cookie explicitly, fail loudly.
            // Otherwise silently skip — the RPC call will report auth errors.
            if (!cookie_file.empty()) {
                std::fprintf(stderr, "bitcoin-tui: %s\n", e.what());
                return 1;
            }
        }
    }

    // Determine if we can offer to launch bitcoind.
    // Requires either an explicit --bitcoind path, or bitcoind found in PATH (localhost only).
    bool can_launch = false;
    if (!bitcoind_cmd.empty()) {
        can_launch = true; // explicit --bitcoind path provided
    } else if (!explicit_host) {
        bitcoind_cmd = find_bitcoind(); // search PATH
        can_launch   = !bitcoind_cmd.empty();
    }

    // State
    AppState   state;
    std::mutex state_mtx;

    // Transaction search state (for / search → overlay)
    TxSearchState              search_state;
    std::vector<TxSearchState> search_history;
    std::mutex                 search_mtx;
    std::string                global_search_str;
    bool                       global_search_active = false;
    std::atomic<bool>          search_in_flight{false};
    std::thread                search_thread;

    // Explorer browse state (block/mempool inline display)
    TxSearchState        browse_block_ss;   // current block browse result
    TxSearchState        browse_mempool_ss; // current mempool browse result
    std::mutex           browse_mtx;
    std::atomic<bool>    browse_in_flight{false};
    std::atomic<int64_t> last_mempool_refresh_time{0};
    std::thread          browse_thread;

    // Broadcast tools state
    BroadcastState    broadcast_state;
    std::mutex        broadcast_mtx;
    bool              tools_input_active = false;
    std::string       tools_hex_str;
    std::atomic<bool> broadcast_in_flight{false};
    std::thread       broadcast_thread;
    int               tools_sel = 0; // 0=Broadcast, 1=result txid (opt), N=AddNode, N+1=BanNode
    int               conn_overlay_sel = 0; // 0=Launch (if can_launch), last=Quit

    // Launch bitcoind state
    std::atomic<bool>        launch_in_flight{false};
    std::atomic<bool>        launch_done{false};
    int                      launch_exit_code = 0;
    std::vector<std::string> launch_output;
    std::mutex               launch_mtx;
    std::thread              launch_thread;

    // Add Node state
    AddNodeState      addnode_state;
    std::mutex        addnode_mtx;
    bool              addnode_input_active = false;
    std::string       addnode_str;
    std::atomic<bool> addnode_in_flight{false};
    std::thread       addnode_thread;

    // Ban Node state
    BanNodeState      ban_state;
    std::mutex        ban_mtx;
    bool              ban_input_active = false;
    std::string       ban_str;
    std::atomic<bool> ban_in_flight{false};
    std::thread       ban_thread;

    // Peer action result (shown in peer detail overlay)
    PeerActionResult  peer_action;
    std::mutex        peer_action_mtx;
    std::atomic<bool> peer_action_in_flight{false};
    std::thread       peer_action_thread;

    // Peer selection state
    int  peer_selected           = -1;
    bool peer_detail_open        = false;
    int  peer_detail_sel         = 0; // 0=Disconnect, 1=Ban
    bool peer_disconnect_overlay = false;

    // Peers tab: added nodes list
    std::vector<AddedNodeInfo> added_nodes;
    std::mutex                 added_nodes_mtx;
    std::atomic<bool>          added_nodes_loaded{false};
    std::atomic<bool>          added_nodes_loading{false};
    std::thread                added_nodes_thread;

    // Peers tab: ban list
    std::vector<BannedEntry> banned_list;
    std::mutex               banned_list_mtx;
    std::atomic<bool>        banned_list_loaded{false};
    std::atomic<bool>        banned_list_loading{false};
    std::thread              banned_list_thread;

    // Peers tab: removed node / unban threads
    std::thread remove_addednode_thread;
    std::thread unban_action_thread;

    // Network tab: softfork deployment info
    std::vector<SoftFork> softforks;
    std::mutex            softforks_mtx;
    std::atomic<bool>     softforks_loaded{false};
    std::atomic<bool>     softforks_loading{false};
    std::thread           softforks_thread;

    // Peers tab: panel focus (0=peers, 1=added nodes, 2=ban list)
    int peers_panel    = 0;
    int addednodes_sel = -1;
    int banlist_sel    = -1;

    // Explorer tab: 0 = mempool block, 1..N = real blocks (recent_blocks[sel-1])
    int         mempool_sel       = 0;
    int         mempool_pane      = -1;   // -1 = unfocused, 0 = blocks pane, 1 = tx list pane
    int         mempool_tx_sel    = -1;   // selected tx in tx list pane
    int         explorer_win_size = 8;    // visible tx rows; updated each render
    std::string mempool_restore_txid;     // txid to restore after mempool refresh
    int         mempool_restore_idx = -1; // fallback position if txid not found

    std::atomic<bool> running{true};

    // FTXUI screen
    auto screen = ScreenInteractive::Fullscreen();

    // Tabs
    std::vector<std::string> tab_labels = {"Dashboard", "Explorer", "Network", "Peers", "Tools"};
    int                      tab_index  = 0;
    int                      prev_tab_index = 0;

    auto tab_toggle = Toggle(&tab_labels, &tab_index);

    // Search trigger for / searches — result always shown as overlay.
    auto trigger_tx_search = [&](const std::string& query, bool switch_tab,
                                 const std::string& blockhash_hint = "", bool push_history = true) {
        if (search_in_flight.load())
            return;
        search_in_flight = true;
        if (switch_tab)
            tab_index = 1;
        {
            std::lock_guard lock(search_mtx);
            if (switch_tab) {
                search_history.clear();
            } else if (push_history && !search_state.txid.empty()) {
                search_history.push_back(search_state);
            }
            search_state           = TxSearchState{};
            search_state.txid      = query;
            search_state.searching = true;
        }
        screen.PostEvent(Event::Custom);

        if (search_thread.joinable())
            search_thread.join();

        int64_t tip_at_search = 0;
        {
            std::lock_guard lock(state_mtx);
            tip_at_search = state.blocks;
        }

        bool query_is_height = !query.empty() && std::ranges::all_of(query, [](unsigned char c) {
            return std::isdigit(c) != 0;
        });

        search_thread = std::thread([&, query, query_is_height, tip_at_search, blockhash_hint] {
            TxSearchState result =
                perform_tx_search(cfg, auth, query, query_is_height, tip_at_search, blockhash_hint);
            search_in_flight = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(search_mtx);
                search_state = result;
            }
            screen.PostEvent(Event::Custom);
        });
    };

    // Browse trigger — fetches block/mempool data for the Explorer inline panes.
    auto trigger_browse_block = [&](int64_t height) {
        if (browse_in_flight.load())
            return;
        browse_in_flight = true;
        if (browse_thread.joinable())
            browse_thread.join();

        browse_thread = std::thread([&, height] {
            TxSearchState result = perform_block_search(cfg, auth, height);
            browse_in_flight     = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(browse_mtx);
                if (result.found && result.is_block)
                    browse_block_ss = std::move(result);
            }
            screen.PostEvent(Event::Custom);
        });
    };

    auto trigger_browse_mempool = [&]() {
        if (browse_in_flight.load())
            return;
        browse_in_flight = true;
        if (browse_thread.joinable())
            browse_thread.join();
        browse_thread = std::thread([&] {
            TxSearchState result = perform_mempool_search(cfg, auth);
            browse_in_flight     = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(browse_mtx);
                if (result.found && result.is_mempool) {
                    browse_mempool_ss         = std::move(result);
                    last_mempool_refresh_time = static_cast<int64_t>(std::time(nullptr));
                }
            }
            screen.PostEvent(Event::Custom);
        });
    };

    // Trigger block/mempool load for the current mempool_sel if not already loaded.
    auto trigger_browse_if_needed = [&]() {
        if (browse_in_flight.load())
            return;
        if (mempool_sel == 0) {
            bool need_load = false;
            {
                std::lock_guard lock(browse_mtx);
                bool            loaded = browse_mempool_ss.is_mempool && browse_mempool_ss.found;
                if (!loaded) {
                    need_load = true;
                } else if (static_cast<int64_t>(std::time(nullptr)) -
                               last_mempool_refresh_time.load() >
                           30) {
                    // Stale: save current selection so we can restore it after refresh.
                    if (mempool_tx_sel >= 0 &&
                        mempool_tx_sel < static_cast<int>(browse_mempool_ss.blk_tx_list.size()))
                        mempool_restore_txid = browse_mempool_ss.blk_tx_list[mempool_tx_sel].txid;
                    mempool_restore_idx = mempool_tx_sel;
                    need_load           = true;
                }
            }
            if (!need_load)
                return;
            trigger_browse_mempool();
        } else {
            int64_t expected_height = -1;
            {
                std::lock_guard lock(state_mtx);
                int             idx = mempool_sel - 1;
                if (idx < static_cast<int>(state.recent_blocks.size()))
                    expected_height = state.recent_blocks[idx].height;
            }
            if (expected_height < 0)
                return;
            {
                std::lock_guard lock(browse_mtx);
                if (browse_block_ss.is_block && browse_block_ss.blk_height == expected_height)
                    return; // already loaded
            }
            trigger_browse_block(expected_height);
        }
    };

    auto open_broadcast_dialog = [&] {
        tools_input_active = true;
        tools_sel          = 0;
        tools_hex_str.clear();
        screen.PostEvent(Event::Custom);
    };

    auto trigger_broadcast = [&](const std::string& hex) {
        if (broadcast_in_flight.load())
            return;
        broadcast_in_flight = true;
        {
            std::lock_guard lock(broadcast_mtx);
            broadcast_state = BroadcastState{.hex = hex, .submitting = true};
        }
        screen.PostEvent(Event::Custom);
        if (broadcast_thread.joinable())
            broadcast_thread.join();
        broadcast_thread = std::thread([&, hex] {
            BroadcastState result{.hex = hex};
            try {
                RpcConfig bcast_cfg       = cfg;
                bcast_cfg.timeout_seconds = 30;
                RpcClient bc(bcast_cfg, auth);
                json      res      = bc.call("sendrawtransaction", {hex});
                result.result_txid = res["result"].get<std::string>();
                result.success     = true;
            } catch (const std::exception& e) {
                result.result_error = e.what();
                result.success      = false;
            }
            result.has_result   = true;
            broadcast_in_flight = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(broadcast_mtx);
                broadcast_state = result;
            }
            screen.PostEvent(Event::Custom);
        });
    };

    auto trigger_addnode = [&](const std::string& addr, const std::string& cmd) {
        if (addnode_in_flight.load())
            return;
        addnode_in_flight = true;
        {
            std::lock_guard lock(addnode_mtx);
            int             saved_cmd = addnode_state.cmd_idx;
            addnode_state             = AddNodeState{.pending = true};
            addnode_state.cmd_idx     = saved_cmd;
        }
        screen.PostEvent(Event::Custom);
        if (addnode_thread.joinable())
            addnode_thread.join();
        addnode_thread = std::thread([&, addr, cmd] {
            AddNodeState result;
            try {
                RpcClient rc(cfg, auth);
                rc.call("addnode", {addr, cmd});
                result.success        = true;
                result.result_message = cmd + " " + addr;
            } catch (const std::exception& e) {
                result.result_message = e.what();
            }
            result.has_result = true;
            addnode_in_flight = false;
            // Refresh added nodes list after addnode operation
            added_nodes_loaded = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(addnode_mtx);
                addnode_state = result;
            }
            screen.PostEvent(Event::Custom);
        });
    };

    auto trigger_setban = [&](const std::string& addr, bool remove) {
        if (ban_in_flight.load())
            return;
        ban_in_flight = true;
        {
            std::lock_guard lock(ban_mtx);
            ban_state = BanNodeState{.pending = true};
        }
        screen.PostEvent(Event::Custom);
        if (ban_thread.joinable())
            ban_thread.join();
        ban_thread = std::thread([&, addr, remove] {
            BanNodeState result;
            try {
                RpcClient   rc(cfg, auth);
                std::string cmd = remove ? "remove" : "add";
                rc.call("setban", {addr, cmd});
                result.success        = true;
                result.result_message = (remove ? "Unbanned " : "Banned ") + addr;
            } catch (const std::exception& e) {
                result.result_message = e.what();
            }
            result.has_result = true;
            ban_in_flight     = false;
            // Refresh ban list after setban operation
            banned_list_loaded = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(ban_mtx);
                ban_state = result;
            }
            screen.PostEvent(Event::Custom);
        });
    };

    // addr should be just the IP (no port) for setban; for disconnectnode use full addr
    auto trigger_peer_action = [&](const std::string& addr, bool is_ban) {
        if (peer_action_in_flight.load())
            return;
        peer_action_in_flight = true;
        {
            std::lock_guard lock(peer_action_mtx);
            peer_action = PeerActionResult{};
        }
        screen.PostEvent(Event::Custom);
        if (peer_action_thread.joinable())
            peer_action_thread.join();
        peer_action_thread = std::thread([&, addr, is_ban] {
            PeerActionResult result;
            try {
                RpcClient rc(cfg, auth);
                if (is_ban) {
                    // Strip port: "[::1]:8333" → "[::1]", "1.2.3.4:8333" → "1.2.3.4"
                    std::string ip = addr;
                    if (!addr.empty() && addr[0] == '[') {
                        auto end = addr.find(']');
                        if (end != std::string::npos)
                            ip = addr.substr(0, end + 1);
                    } else {
                        auto colon = addr.rfind(':');
                        if (colon != std::string::npos)
                            ip = addr.substr(0, colon);
                    }
                    rc.call("setban", {ip, std::string("add")});
                    result.message = "Banned " + ip;
                } else {
                    rc.call("disconnectnode", {addr});
                    result.message = "Disconnected";
                }
                result.success = true;
            } catch (const std::exception& e) {
                result.message = e.what();
            }
            result.has_result     = true;
            peer_action_in_flight = false;
            if (is_ban)
                banned_list_loaded = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(peer_action_mtx);
                peer_action = result;
            }
            screen.PostEvent(Event::Custom);
        });
    };

    auto fetch_added_nodes = [&] {
        if (added_nodes_loading.load())
            return;
        if (added_nodes_thread.joinable())
            added_nodes_thread.join();
        added_nodes_loading = true;
        added_nodes_thread  = std::thread([&] {
            std::vector<AddedNodeInfo> result;
            try {
                RpcClient rc(cfg, auth);
                auto      ans = rc.call("getaddednodeinfo")["result"];
                for (const auto& n : ans) {
                    AddedNodeInfo info;
                    info.addednode = n.value("addednode", "");
                    if (n.contains("addresses") && n["addresses"].is_array()) {
                        for (const auto& a : n["addresses"]) {
                            if (a.value("connected", false)) {
                                info.connected = true;
                                break;
                            }
                        }
                    }
                    result.push_back(std::move(info));
                }
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            if (!running.load())
                return;
            {
                std::lock_guard lock(added_nodes_mtx);
                added_nodes = std::move(result);
            }
            added_nodes_loading = false;
            added_nodes_loaded  = true;
            screen.PostEvent(Event::Custom);
        });
    };

    auto fetch_ban_list = [&] {
        if (banned_list_loading.load())
            return;
        if (banned_list_thread.joinable())
            banned_list_thread.join();
        banned_list_loading = true;
        banned_list_thread  = std::thread([&] {
            std::vector<BannedEntry> result;
            try {
                RpcClient rc(cfg, auth);
                auto      bl = rc.call("listbanned")["result"];
                for (const auto& b : bl) {
                    BannedEntry entry;
                    entry.address      = b.value("address", "");
                    entry.banned_until = b.value("banned_until", 0LL);
                    entry.ban_reason   = b.value("ban_reason", "");
                    result.push_back(std::move(entry));
                }
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            if (!running.load())
                return;
            {
                std::lock_guard lock(banned_list_mtx);
                banned_list = std::move(result);
            }
            banned_list_loading = false;
            banned_list_loaded  = true;
            screen.PostEvent(Event::Custom);
        });
    };

    auto do_remove_added_node = [&](const std::string& addr) {
        if (remove_addednode_thread.joinable())
            remove_addednode_thread.join();
        remove_addednode_thread = std::thread([&, addr] {
            try {
                RpcClient rc(cfg, auth);
                rc.call("addnode", {addr, std::string("remove")});
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            if (!running.load())
                return;
            added_nodes_loaded  = false;
            added_nodes_loading = false;
            screen.PostEvent(Event::Custom);
        });
    };

    auto do_unban = [&](const std::string& addr) {
        if (unban_action_thread.joinable())
            unban_action_thread.join();
        unban_action_thread = std::thread([&, addr] {
            try {
                RpcClient rc(cfg, auth);
                rc.call("setban", {addr, std::string("remove")});
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            if (!running.load())
                return;
            banned_list_loaded  = false;
            banned_list_loading = false;
            screen.PostEvent(Event::Custom);
        });
    };

    auto fetch_softforks = [&] {
        if (softforks_loading.load())
            return;
        if (softforks_thread.joinable())
            softforks_thread.join();
        softforks_loading = true;
        softforks_thread  = std::thread([&] {
            std::vector<SoftFork> result;
            try {
                RpcClient rc(cfg, auth);
                auto      dep = rc.call("getdeploymentinfo")["result"]["deployments"];
                if (dep.is_object()) {
                    for (const auto& [name, val] : dep.items()) {
                        SoftFork f;
                        f.name   = name;
                        f.type   = val.value("type", std::string{});
                        f.active = val.value("active", false);
                        f.height = val.value("height", int64_t{-1});
                        if (val.contains("bip9") && val["bip9"].is_object()) {
                            const auto& b9        = val["bip9"];
                            f.bip9_status         = b9.value("status", std::string{});
                            f.bip9_since          = b9.value("since", int64_t{0});
                            f.bip9_start_time     = b9.value("start_time", int64_t{0});
                            f.bip9_timeout        = b9.value("timeout", int64_t{0});
                            f.bip9_min_activation = b9.value("min_activation_height", int64_t{0});
                            if (b9.contains("statistics") && b9["statistics"].is_object()) {
                                const auto& st   = b9["statistics"];
                                f.bip9_elapsed   = st.value("elapsed", int64_t{0});
                                f.bip9_count     = st.value("count", int64_t{0});
                                f.bip9_period    = st.value("period", int64_t{0});
                                f.bip9_threshold = st.value("threshold", int64_t{0});
                            }
                        }
                        result.push_back(std::move(f));
                    }
                    std::sort(result.begin(), result.end(),
                               [](const SoftFork& a, const SoftFork& b) {
                                  if (a.active != b.active)
                                      return a.active > b.active;
                                  return a.name < b.name;
                              });
                }
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            if (!running.load())
                return;
            {
                std::lock_guard lock(softforks_mtx);
                softforks = std::move(result);
            }
            softforks_loading = false;
            softforks_loaded  = true;
            screen.PostEvent(Event::Custom);
        });
    };

    // Layout: tab toggle only — global search is handled via '/' key in the event handler
    // (The Toggle component consumes Tab internally, so FTXUI Input focus is unreachable.)
    auto layout = Container::Vertical({tab_toggle});

    auto renderer = Renderer(layout, [&]() -> Element {
        // Snapshot state (brief lock)
        AppState snap;
        {
            std::lock_guard lock(state_mtx);
            snap = state;
        }

        // Is a search result overlay currently visible?
        bool          overlay_visible;
        bool          overlay_is_confirmed_tx;    // confirmed tx (not a block)
        bool          overlay_block_row_selected; // block # row highlighted (io_selected == 0)
        bool          overlay_inputs_row_sel;     // inputs row highlighted
        bool          overlay_outputs_row_sel;    // outputs row highlighted
        bool          overlay_inputs_open;        // inputs sub-overlay is open
        bool          overlay_outputs_open;       // outputs sub-overlay is open
        TxSearchState search_snap;                // copy of search_state for overlay render
        {
            std::lock_guard lock(search_mtx);
            overlay_visible         = !search_state.txid.empty();
            overlay_is_confirmed_tx = overlay_visible && search_state.found &&
                                      search_state.confirmed && !search_state.is_block;
            int sel                    = search_state.io_selected;
            int inputs_idx             = io_inputs_idx(search_state);
            int outputs_idx            = io_outputs_idx(search_state);
            overlay_block_row_selected = overlay_is_confirmed_tx && sel == 0;
            overlay_inputs_row_sel =
                overlay_is_confirmed_tx && sel == inputs_idx && inputs_idx >= 0;
            overlay_outputs_row_sel =
                overlay_is_confirmed_tx && sel == outputs_idx && outputs_idx >= 0;
            overlay_inputs_open  = overlay_is_confirmed_tx && search_state.inputs_overlay_open;
            overlay_outputs_open = overlay_is_confirmed_tx && search_state.outputs_overlay_open;
            if (overlay_visible)
                search_snap = search_state;
        }

        // Tab content
        Element tab_content;
        switch (tab_index) {
        case 0:
            tab_content = render_dashboard(snap);
            break;
        case 1: {
            // Hold browse_mtx for the Explorer tab render to avoid copying
            // TxSearchState (blk_tx_list can be 100k+ entries for mempool).
            std::lock_guard      lock(browse_mtx);
            const TxSearchState& browse = (mempool_sel == 0) ? browse_mempool_ss : browse_block_ss;

            // Panes 1-3 always rendered inline via render_mempool.
            auto base = vbox({render_mempool(snap, mempool_sel, browse, mempool_tx_sel,
                                             mempool_pane, explorer_win_size),
                              filler()}) |
                        flex;

            // No search overlay on Explorer tab — just show the browse panes.
            if (!overlay_visible) {
                tab_content = std::move(base);
                break;
            }

            // Overlay a search result on top of the Explorer base.
            const auto& ss = search_snap;

            // Abbreviated txid: first 20 + "…" + last 20
            std::string txid_abbrev = ss.txid.size() > 40 ? ss.txid.substr(0, 20) + "…" +
                                                                ss.txid.substr(ss.txid.size() - 20)
                                                          : ss.txid;

            Elements result_rows;
            switch (classify_result(ss)) {
            case TxResultKind::Searching:
                result_rows.push_back(text("  Searching…") | color(Color::Yellow));
                break;
            case TxResultKind::Block: {
                auto               blk_time_t = static_cast<std::time_t>(ss.blk_time);
                auto*              blk_tm_ptr = std::localtime(&blk_time_t);
                std::tm            blk_tm     = blk_tm_ptr ? *blk_tm_ptr : std::tm{};
                std::ostringstream blk_time_ss;
                blk_time_ss << std::put_time(&blk_tm, "%Y-%m-%d %H:%M:%S");

                int64_t blk_age =
                    ss.blk_time > 0
                        ? std::max(int64_t{0},
                                   static_cast<int64_t>(std::time(nullptr)) - ss.blk_time)
                        : int64_t{0};

                std::ostringstream diff_ss;
                diff_ss << std::fixed << std::setprecision(2) << ss.blk_difficulty / 1e12 << " T";

                std::string hash_short = ss.blk_hash.size() > 48
                                             ? ss.blk_hash.substr(0, 4) + "…" +
                                                   ss.blk_hash.substr(ss.blk_hash.size() - 44)
                                             : ss.blk_hash;

                result_rows.push_back(text("  ⛏ BLOCK") | color(Color::Cyan) | bold);
                result_rows.push_back(label_value("  Height       : ", fmt_height(ss.blk_height)));
                result_rows.push_back(label_value("  Hash         : ", hash_short));
                result_rows.push_back(label_value("  Time         : ", blk_time_ss.str()));
                result_rows.push_back(
                    label_value("  Age          : ", ss.blk_time > 0 ? fmt_age(blk_age) : "—"));
                result_rows.push_back(label_value("  Transactions : ", fmt_int(ss.blk_ntx)));
                result_rows.push_back(
                    label_value("  Size         : ", fmt_int(ss.blk_size) + " B"));
                result_rows.push_back(
                    label_value("  Weight       : ", fmt_int(ss.blk_weight) + " WU"));
                result_rows.push_back(label_value("  Difficulty   : ", diff_ss.str()));
                result_rows.push_back(label_value("  Miner        : ", ss.blk_miner));
                result_rows.push_back(
                    label_value("  Confirmations: ", fmt_int(ss.blk_confirmations)));
                break;
            }
            case TxResultKind::Mempool: {
                std::ostringstream rate_ss;
                rate_ss << std::fixed << std::setprecision(1) << ss.fee_rate << " sat/vB";

                int64_t age =
                    std::max(int64_t{0}, static_cast<int64_t>(std::time(nullptr)) - ss.entry_time);

                result_rows.push_back(text("  ● MEMPOOL") | color(Color::Yellow) | bold);
                result_rows.push_back(
                    label_value("  Fee         : ", fmt_btc(ss.fee), Color::Green));
                result_rows.push_back(label_value("  Fee rate    : ", rate_ss.str()));
                result_rows.push_back(label_value("  vsize       : ", fmt_int(ss.vsize) + " vB"));
                result_rows.push_back(label_value("  Weight      : ", fmt_int(ss.weight) + " WU"));
                result_rows.push_back(label_value("  Ancestors   : ", fmt_int(ss.ancestors)));
                result_rows.push_back(label_value("  Descendants : ", fmt_int(ss.descendants)));
                result_rows.push_back(label_value("  In mempool  : ", fmt_age(age)));
                break;
            }
            case TxResultKind::Confirmed: {
                int64_t age = ss.blocktime > 0
                                  ? std::max(int64_t{0}, static_cast<int64_t>(std::time(nullptr)) -
                                                             ss.blocktime)
                                  : int64_t{0};

                std::string block_num = ss.block_height >= 0 ? fmt_height(ss.block_height) : "—";

                result_rows.push_back(text("  ✔ CONFIRMED") | color(Color::Green) | bold);
                result_rows.push_back(label_value("  Confirmations: ", fmt_int(ss.confirmations)));
                {
                    auto block_row =
                        hbox({text("  Block #      : ") | color(Color::GrayDark),
                              text(block_num) | color(Color::Cyan) | underlined, filler()});
                    if (ss.io_selected == 0)
                        block_row = std::move(block_row) | inverted;
                    result_rows.push_back(std::move(block_row));
                }
                auto bh_short =
                    ss.blockhash.substr(0, 4) + "…" + ss.blockhash.substr(ss.blockhash.size() - 44);
                result_rows.push_back(label_value("  Block hash   : ", bh_short));
                result_rows.push_back(
                    label_value("  Block age    : ", ss.blocktime > 0 ? fmt_age(age) : "—"));
                result_rows.push_back(label_value("  vsize        : ", fmt_int(ss.vsize) + " vB"));
                result_rows.push_back(label_value("  Weight       : ", fmt_int(ss.weight) + " WU"));
                if (!ss.vin_list.empty()) {
                    auto inputs_row = hbox(
                        {text("  Inputs       : ") | color(Color::GrayDark),
                         text(std::to_string(ss.vin_list.size())) | color(Color::Cyan) | underlined,
                         filler()});
                    if (ss.io_selected == 1)
                        inputs_row = std::move(inputs_row) | inverted;
                    result_rows.push_back(std::move(inputs_row));
                }
                if (!ss.vout_list.empty()) {
                    int  outputs_idx = io_outputs_idx(ss);
                    auto outputs_row = hbox({text("  Outputs      : ") | color(Color::GrayDark),
                                             text(std::to_string(ss.vout_list.size())) |
                                                 color(Color::Cyan) | underlined,
                                             filler()});
                    if (ss.io_selected == outputs_idx)
                        outputs_row = std::move(outputs_row) | inverted;
                    result_rows.push_back(std::move(outputs_row));
                }
                result_rows.push_back(
                    label_value("  Total out    : ", fmt_btc(ss.total_output), Color::Green));
                break;
            }
            case TxResultKind::Error:
                result_rows.push_back(text("  " + ss.error) | color(Color::Red));
                break;
            }

            std::string   overlay_title = classify_result(ss) == TxResultKind::Block
                                              ? " Block Search "
                                              : " Transaction Search ";
            constexpr int kPanelWidth   = 70;
            auto          overlay_panel = vbox({
                                     hbox({
                                         text(overlay_title) | bold | color(Color::Gold1),
                                         filler(),
                                         text(" " + txid_abbrev + " ") | color(Color::GrayDark),
                                     }),
                                     separator(),
                                     vbox(std::move(result_rows)),
                                 }) |
                                 border | size(WIDTH, EQUAL, kPanelWidth);

            // io/vout overlays use a wider panel so full txids fit
            constexpr int kIOPanelWidth = 84;

            auto build_io_panel = [&](std::string title, Elements rows, int n, int win,
                                      int top) -> Element {
                if (n > win) {
                    rows.push_back(hbox(
                        {filler(), text(std::to_string(top + 1) + "–" + std::to_string(top + win) +
                                        " / " + std::to_string(n)) |
                                       color(Color::GrayDark)}));
                }
                return vbox({hbox({text(std::move(title)) | bold | color(Color::Gold1), filler(),
                                   text(" " + txid_abbrev + " ") | color(Color::GrayDark)}),
                             separator(), vbox(std::move(rows))}) |
                       border | size(WIDTH, EQUAL, kIOPanelWidth);
            };

            if (ss.outputs_overlay_open && !ss.vout_list.empty()) {
                int n   = static_cast<int>(ss.vout_list.size());
                int win = std::min(n, 10);
                int top = 0;
                int sel = ss.output_overlay_sel;
                if (sel >= 0) {
                    top = std::max(0, sel - win / 2);
                    top = std::min(top, n - win);
                }
                Elements rows;
                for (int i = top; i < top + win; ++i) {
                    const auto&        v = ss.vout_list[i];
                    std::ostringstream val_ss;
                    val_ss << std::fixed << std::setprecision(8) << v.value;
                    std::string label = val_ss.str() + " BTC";
                    if (!v.address.empty()) {
                        // Panel is 84 chars; after prefix + value that leaves ~60 for address.
                        // Taproot bc1p addresses are 62 chars — truncate only if needed.
                        std::string addr = v.address.size() > 60
                                               ? v.address.substr(0, 28) + "…" +
                                                     v.address.substr(v.address.size() - 28)
                                               : v.address;
                        label += "  " + addr;
                    } else if (!v.type.empty()) {
                        label += "  [" + v.type + "]";
                    }
                    bool selected = (i == sel);
                    auto row =
                        hbox({text("  [" + std::to_string(i) + "] ") | color(Color::GrayDark),
                              text(label)});
                    if (selected)
                        row = std::move(row) | inverted;
                    rows.push_back(std::move(row));
                }
                tab_content = vbox({filler(),
                                    hbox({filler(),
                                          build_io_panel(" Outputs (" + std::to_string(n) + ") ",
                                                         std::move(rows), n, win, top),
                                          filler()}),
                                    filler()}) |
                              flex;
            } else if (ss.inputs_overlay_open && !ss.vin_list.empty()) {
                int n   = static_cast<int>(ss.vin_list.size());
                int win = std::min(n, 10);
                int top = 0;
                int sel = ss.input_overlay_sel;
                if (sel >= 0) {
                    top = std::max(0, sel - win / 2);
                    top = std::min(top, n - win);
                }
                Elements rows;
                for (int i = top; i < top + win; ++i) {
                    const auto& v = ss.vin_list[i];
                    std::string label =
                        v.is_coinbase ? "coinbase" : v.txid + ":" + std::to_string(v.vout);
                    bool selected = (i == sel);
                    auto row =
                        hbox({text("  [" + std::to_string(i) + "] ") | color(Color::GrayDark),
                              text(label) | (v.is_coinbase ? color(Color::GrayDark)
                                                           : color(Color::Default))});
                    if (selected)
                        row = std::move(row) | inverted;
                    rows.push_back(std::move(row));
                }
                tab_content = vbox({filler(),
                                    hbox({filler(),
                                          build_io_panel(" Inputs (" + std::to_string(n) + ") ",
                                                         std::move(rows), n, win, top),
                                          filler()}),
                                    filler()}) |
                              flex;
            } else {
                tab_content = vbox({
                                  filler(),
                                  hbox({filler(), std::move(overlay_panel), filler()}),
                                  filler(),
                              }) |
                              flex;
            }
            break;
        }
        case 2: {
            if (!softforks_loaded.load() && !softforks_loading.load())
                fetch_softforks();
            std::vector<SoftFork> forks_snap;
            {
                std::lock_guard lock(softforks_mtx);
                forks_snap = softforks;
            }
            tab_content = render_network(snap, forks_snap, softforks_loading.load());
            break;
        }
        case 3:
            if (peer_disconnect_overlay) {
                PeerActionResult action_snap;
                {
                    std::lock_guard lock(peer_action_mtx);
                    action_snap = peer_action;
                }
                bool    in_flight = peer_action_in_flight.load();
                Element msg;
                if (in_flight || !action_snap.has_result) {
                    msg = text("  Working\u2026  ") | bold | color(Color::Yellow);
                } else if (action_snap.success) {
                    msg = vbox({
                        text("  \u2713 " + action_snap.message + "  ") | bold | color(Color::Green),
                        text("  [Esc] to dismiss  ") | color(Color::GrayDark),
                    });
                } else {
                    msg = vbox({
                        text("  \u2717 " + action_snap.message + "  ") | bold | color(Color::Red),
                        text("  [Esc] to dismiss  ") | color(Color::GrayDark),
                    });
                }
                tab_content = vbox({
                                  filler(),
                                  hbox({filler(), msg | border, filler()}),
                                  filler(),
                              }) |
                              flex;
            } else if (peer_detail_open && peer_selected >= 0 &&
                       peer_selected < static_cast<int>(snap.peers.size())) {
                PeerActionResult action_snap;
                {
                    std::lock_guard lock(peer_action_mtx);
                    action_snap = peer_action;
                }
                tab_content = vbox({
                                  filler(),
                                  hbox({filler(),
                                        render_peer_detail(snap.peers[peer_selected], action_snap,
                                                           peer_detail_sel),
                                        filler()}),
                                  filler(),
                              }) |
                              flex;
            } else if (addnode_input_active) {
                AddNodeState addnode_snap;
                {
                    std::lock_guard lock(addnode_mtx);
                    addnode_snap = addnode_state;
                }
                tab_content =
                    vbox({filler(),
                          hbox({filler(), render_addnode_overlay(addnode_snap, addnode_str),
                                filler()}),
                          filler()}) |
                    flex;
            } else if (ban_input_active) {
                BanNodeState ban_snap;
                {
                    std::lock_guard lock(ban_mtx);
                    ban_snap = ban_state;
                }
                tab_content =
                    vbox({filler(),
                          hbox({filler(), render_ban_overlay(ban_snap, ban_str), filler()}),
                          filler()}) |
                    flex;
            } else if (peers_panel == 1) {
                if (!added_nodes_loaded.load() && !added_nodes_loading.load())
                    fetch_added_nodes();
                std::vector<AddedNodeInfo> an_snap;
                {
                    std::lock_guard lock(added_nodes_mtx);
                    an_snap = added_nodes;
                }
                tab_content = vbox({
                                  filler(),
                                  hbox({filler(),
                                        render_added_nodes_panel(
                                            an_snap, added_nodes_loading.load(), addednodes_sel),
                                        filler()}),
                                  filler(),
                              }) |
                              flex;
            } else if (peers_panel == 2) {
                if (!banned_list_loaded.load() && !banned_list_loading.load())
                    fetch_ban_list();
                std::vector<BannedEntry> bl_snap;
                {
                    std::lock_guard lock(banned_list_mtx);
                    bl_snap = banned_list;
                }
                tab_content = vbox({
                                  filler(),
                                  hbox({filler(),
                                        render_ban_list_panel(bl_snap, banned_list_loading.load(),
                                                              banlist_sel),
                                        filler()}),
                                  filler(),
                              }) |
                              flex;
            } else {
                tab_content = render_peers(snap, peer_selected) | flex;
            }
            break;
        case 4: {
            BroadcastState bs;
            {
                std::lock_guard lock(broadcast_mtx);
                bs = broadcast_state;
            }
            tab_content = render_tools(snap, bs, tools_input_active, tools_hex_str, tools_sel);
            break;
        }
        default:
            tab_content = text("Unknown tab");
        }

        // Status / connection indicator
        Element status_left;
        if (!snap.connected && !snap.error_message.empty()) {
            status_left = hbox({
                text(" ERROR ") | bgcolor(Color::Red) | color(Color::White) | bold,
                text(" " + snap.error_message) | color(Color::Red),
            });
        } else {
            status_left = hbox({
                text(" "),
                snap.connected ? text("● CONNECTED") | color(Color::Green) | bold
                               : text("○ CONNECTING…") | color(Color::Yellow) | bold,
                text("  Last update: " + snap.last_update) | color(Color::GrayDark),
            });
        }

        auto refresh_indicator =
            snap.refreshing
                ? text(" ↻ refreshing") | color(Color::Yellow)
                : text(" ↻ every " + std::to_string(refresh_secs) + "s") | color(Color::GrayDark);
        Element status_right =
            global_search_active
                ? hbox({text("  [Enter] search  [Esc] cancel ") | color(Color::Yellow)})
            : (tab_index == 4 && tools_input_active)
                ? hbox({text("  [Enter] submit  [Esc] cancel ") | color(Color::Yellow)})
            : (tab_index == 3 && addnode_input_active)
                ? hbox({text("  [Enter] submit  [←/→] change  [Esc] cancel ") |
                        color(Color::Yellow)})
            : (tab_index == 3 && ban_input_active)
                ? hbox({text("  [Enter] submit  [←/→] ban\u2215unban  [Esc] cancel ") |
                        color(Color::Yellow)})
            : (tab_index == 3 && peer_disconnect_overlay && !peer_action_in_flight.load())
                ? hbox({text("  [Esc] dismiss  [q] quit ") | color(Color::Yellow)})
            : (tab_index == 3 && peer_disconnect_overlay)
                ? hbox({text("  [q] quit ") | color(Color::GrayDark)})
            : (tab_index == 3 && peers_panel == 1)
                ? hbox({text("  [\u2191/\u2193] navigate  [\u23ce] remove  [a] add node  [Esc] "
                             "close  [q] quit ") |
                        color(Color::Yellow)})
            : (tab_index == 3 && peers_panel == 2)
                ? hbox({text("  [\u2191/\u2193] navigate  [\u23ce] unban  [Esc] close  [q] quit ") |
                        color(Color::Yellow)})
            : (tab_index == 3 && peer_detail_open)
                ? hbox({text("  [\u2190/\u2192] select action  [\u23ce] confirm  [Esc] back  [q] "
                             "quit ") |
                        color(Color::Yellow)})
            : (tab_index == 4)
                ? hbox({text("  [↑/↓] navigate  [↵] select  [q] quit ") | color(Color::GrayDark)})
            : overlay_outputs_open
                ? hbox({text("  [↑/↓] navigate  [Esc] back  [q] quit ") | color(Color::Yellow)})
            : overlay_inputs_open
                ? hbox({text("  [↑/↓] navigate  [↵] lookup  [Esc] back  [q] quit ") |
                        color(Color::Yellow)})
            : overlay_outputs_row_sel
                ? hbox({text("  [↵] show outputs  [↑/↓] navigate  [Esc] dismiss  [q] quit ") |
                        color(Color::Yellow)})
            : overlay_inputs_row_sel
                ? hbox({text("  [↵] show inputs  [↑/↓] navigate  [Esc] dismiss  [q] quit ") |
                        color(Color::Yellow)})
            : overlay_block_row_selected
                ? hbox({text("  [↵] view block  [↑/↓] navigate  [Esc] dismiss  [q] quit ") |
                        color(Color::Yellow)})
            : overlay_is_confirmed_tx
                ? hbox({text("  [↑/↓] navigate  [Esc] dismiss  [q] quit ") | color(Color::Yellow)})
            : overlay_visible ? hbox({text("  [Esc] dismiss  [q] quit ") | color(Color::Yellow)})
            : (tab_index == 1 && mempool_pane == 1)
                ? hbox({text("  [↑/↓] navigate  [↵] lookup tx  [Esc] back  [q] quit ") |
                        color(Color::Yellow)})
            : (tab_index == 1 && mempool_pane == 0)
                ? hbox({text("  [↑/↓/←/→] navigate  [/] search  [q] quit ") | color(Color::Yellow)})
            : (tab_index == 1) ? hbox({refresh_indicator,
                                       text("  [↓] select  [←/→] navigate  [/] search  [q] quit ") |
                                           color(Color::GrayDark)})
            : (tab_index == 3 && peer_selected >= 0)
                ? hbox({refresh_indicator, text("  [\u2191/\u2193] navigate  [\u23ce] details  [a] "
                                                "added nodes  [b] ban list  [q] quit ") |
                                               color(Color::GrayDark)})
            : (tab_index == 3)
                ? hbox(
                      {refresh_indicator,
                       text(
                           "  [\u2191/\u2193] navigate  [a] added nodes  [b] ban list  [q] quit ") |
                           color(Color::GrayDark)})
                : hbox({refresh_indicator, text("  [Tab/←/→] switch  [/] search  [q] quit ") |
                                               color(Color::GrayDark)});

        return vbox({
            // Title bar
            hbox({
                text(" ₿ Bitcoin Core TUI ") | bold | color(Color::Gold1),
                text(" " + cfg.host + ":" + std::to_string(cfg.port) + " ") |
                    color(Color::GrayDark),
                filler(),
                snap.chain.empty() || snap.chain == "—"
                    ? text("")
                    : text(" " + snap.chain + " ") | bold |
                          (snap.chain == "main" ? bgcolor(Color::DarkGreen)
                                                : bgcolor(Color::Yellow)) |
                          (snap.chain == "main" ? color(Color::White) : color(Color::Black)),
            }) | border,

            // Tab bar with global search on the right
            // Render tabs manually from tab_index so the highlight is always in sync,
            // even when tab_index is set directly (toggle's focused_entry won't drift).
            hbox({
                [&] {
                    Elements tabs;
                    for (int i = 0; i < (int)tab_labels.size(); ++i) {
                        if (i > 0)
                            tabs.push_back(text("│") | automerge);
                        auto e = text(" " + tab_labels[i] + " ");
                        tabs.push_back(i == tab_index && snap.connected ? e | bold | inverted
                                                                        : e | dim);
                    }
                    return hbox(std::move(tabs)) | flex;
                }(),
                separatorLight(),
                [&]() -> Element {
                    if (!global_search_active)
                        return text(" / search ") | color(Color::GrayDark) | size(WIDTH, EQUAL, 16);
                    // Cursor-following: show a window ending at the cursor
                    constexpr int kWidth    = 46;
                    constexpr int kTextCols = kWidth - 3; // " " + text + "│"
                    std::string   vis       = global_search_str;
                    if ((int)vis.size() > kTextCols)
                        vis = vis.substr(vis.size() - kTextCols);
                    return hbox({text(" "), text(vis) | color(Color::White),
                                 text("│") | color(Color::White), filler()}) |
                           size(WIDTH, EQUAL, kWidth);
                }(),
            }) | border,

            // Content — show connection overlay when disconnected
            snap.connected
                ? tab_content | flex
                : vbox({
                      filler(),
                      hbox({filler(),
                            vbox({
                                hbox({text(" Connection Failed - Reconnecting ") | bold |
                                          color(Color::Red),
                                      filler()}),
                                separator(),
                                hbox({text(" Network : ") | color(Color::GrayDark),
                                      text(network) | color(Color::White)}),
                                hbox({text(" RPC port: ") | color(Color::GrayDark),
                                      text(cfg.host + ":" + std::to_string(cfg.port)) |
                                          color(Color::White)}),
                                hbox({text(" Datadir : ") | color(Color::GrayDark),
                                      text(datadir) | color(Color::White)}),
                                hbox({text(" Auth    : ") | color(Color::GrayDark),
                                      text(explicit_creds
                                               ? auth.get().user + ":<hidden>"
                                               : "cookie (" + cookie_path(network, datadir) + ")") |
                                          color(Color::White)}),
                                [&]() -> Element {
                                    if (launch_in_flight.load() || launch_done.load()) {
                                        Elements rows;
                                        rows.push_back(
                                            text(launch_in_flight.load() ? "  Launching bitcoind…"
                                                 : launch_exit_code == 0 ? "  ✓ bitcoind started"
                                                                         : "  ✗ bitcoind failed") |
                                            color(launch_in_flight.load() ? Color::Yellow
                                                  : launch_exit_code == 0 ? Color::Green
                                                                          : Color::Red) |
                                            bold);
                                        {
                                            std::lock_guard lock(launch_mtx);
                                            for (auto& line : launch_output)
                                                rows.push_back(paragraph("  " + line) |
                                                               color(Color::White));
                                        }
                                        return vbox(std::move(rows));
                                    }
                                    return hbox({text(" Error   : ") | color(Color::GrayDark),
                                                 paragraph(snap.error_message.empty()
                                                               ? std::string("connecting…")
                                                               : snap.error_message) |
                                                     color(Color::Yellow)});
                                }(),
                                separator(),
                                hbox({
                                    text("  "),
                                    can_launch
                                        ? text(" Launch bitcoind ") |
                                              (!launch_in_flight.load() && !launch_done.load() &&
                                                       conn_overlay_sel == 0
                                                   ? inverted
                                                   : dim)
                                        : text(""),
                                    filler(),
                                    text(" Quit ") |
                                        (!launch_in_flight.load() &&
                                                 conn_overlay_sel == (can_launch ? 1 : 0)
                                             ? inverted
                                             : dim),
                                    text("  "),
                                }),
                            }) | border |
                                size(WIDTH, EQUAL, 80),
                            filler()}),
                      filler(),
                  }) | flex,

            // Status bar
            hbox({status_left, filler(), status_right}) | border,
        });
    });

    // Event handling: '/' activates global search; Escape/Enter commit or cancel
    auto event_handler = CatchEvent(renderer, [&](const Event& event) -> bool {
        {
            // Connection overlay: navigate buttons, launch or quit; consume all events
            std::lock_guard lock(state_mtx);
            if (state.connected) {
                launch_done = false;
            } else {
                int max_sel = can_launch ? 1 : 0;
                if (event == Event::Escape || event == Event::Character('q')) {
                    screen.ExitLoopClosure()();
                }
                if (event == Event::Return) {
                    if (can_launch && conn_overlay_sel == 0 && !launch_in_flight.load() &&
                        !launch_done.load()) {
                        launch_in_flight = true;
                        launch_done      = false;
                        {
                            std::lock_guard launch_lock(launch_mtx);
                            launch_output.clear();
                        }
                        if (launch_thread.joinable())
                            launch_thread.join();
                        launch_thread = std::thread([&] {
                            launch_exit_code = launch_bitcoind(
                                bitcoind_cmd, datadir, network, [&](const std::string& line) {
                                    {
                                        std::lock_guard launch_lock(launch_mtx);
                                        launch_output.push_back(line);
                                    }
                                    screen.PostEvent(Event::Custom);
                                });
                            launch_in_flight = false;
                            launch_done      = true;
                            screen.PostEvent(Event::Custom);
                        });
                    } else if (!launch_in_flight.load()) {
                        screen.ExitLoopClosure()();
                    }
                } else if (event == Event::ArrowLeft || event == Event::ArrowRight) {
                    if (event == Event::ArrowLeft)
                        conn_overlay_sel = std::max(conn_overlay_sel - 1, 0);
                    else
                        conn_overlay_sel = std::min(conn_overlay_sel + 1, max_sel);
                    screen.PostEvent(Event::Custom);
                }
                return true;
            }
        }
        if (global_search_active) {
            if (event == Event::Escape) {
                global_search_active = false;
                global_search_str.clear();
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Return) {
                std::string q        = trimmed(global_search_str);
                global_search_active = false;
                global_search_str.clear();
                if (is_txid(q) || is_height(q))
                    trigger_tx_search(q, true);
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Backspace) {
                if (!global_search_str.empty())
                    global_search_str.pop_back();
                screen.PostEvent(Event::Custom);
                return true;
            }
            // Swallow Tab/arrows so they don't change tabs while typing
            if (event == Event::Tab || event == Event::TabReverse || event == Event::ArrowLeft ||
                event == Event::ArrowRight)
                return true;
            if (event.is_character()) {
                global_search_str += event.character();
                screen.PostEvent(Event::Custom);
                return true;
            }
            return false;
        }
        // Tools broadcast input mode
        if (tools_input_active) {
            if (event == Event::Escape) {
                tools_input_active = false;
                tools_hex_str.clear();
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Return) {
                std::string hex    = trimmed(tools_hex_str);
                tools_input_active = false;
                tools_hex_str.clear();
                if (!hex.empty())
                    trigger_broadcast(hex);
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Backspace) {
                if (!tools_hex_str.empty())
                    tools_hex_str.pop_back();
                screen.PostEvent(Event::Custom);
                return true;
            }
            // Swallow Tab/arrows so they don't change tabs while typing
            if (event == Event::Tab || event == Event::TabReverse || event == Event::ArrowLeft ||
                event == Event::ArrowRight)
                return true;
            if (event.is_character()) {
                tools_hex_str += event.character();
                screen.PostEvent(Event::Custom);
                return true;
            }
            return false;
        }
        // Outputs sub-overlay mode
        {
            bool outputs_open = false;
            {
                std::lock_guard lock(search_mtx);
                outputs_open = search_state.found && search_state.confirmed &&
                               !search_state.is_block && search_state.outputs_overlay_open;
            }
            if (outputs_open) {
                if (event == Event::Escape) {
                    {
                        std::lock_guard lock(search_mtx);
                        search_state.outputs_overlay_open = false;
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowDown || event == Event::ArrowUp) {
                    {
                        std::lock_guard lock(search_mtx);
                        int             n = static_cast<int>(search_state.vout_list.size());
                        if (event == Event::ArrowDown)
                            search_state.output_overlay_sel =
                                std::min(search_state.output_overlay_sel + 1, n - 1);
                        else
                            search_state.output_overlay_sel =
                                std::max(search_state.output_overlay_sel - 1, -1);
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::Character('q')) {
                    screen.ExitLoopClosure()();
                    return true;
                }
                return false;
            }
        }
        // Inputs sub-overlay mode
        {
            bool inputs_open = false;
            {
                std::lock_guard lock(search_mtx);
                inputs_open = search_state.found && search_state.confirmed &&
                              !search_state.is_block && search_state.inputs_overlay_open;
            }
            if (inputs_open) {
                if (event == Event::Escape) {
                    {
                        std::lock_guard lock(search_mtx);
                        search_state.inputs_overlay_open = false;
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowDown || event == Event::ArrowUp) {
                    {
                        std::lock_guard lock(search_mtx);
                        int             n = static_cast<int>(search_state.vin_list.size());
                        if (event == Event::ArrowDown)
                            search_state.input_overlay_sel =
                                std::min(search_state.input_overlay_sel + 1, n - 1);
                        else
                            search_state.input_overlay_sel =
                                std::max(search_state.input_overlay_sel - 1, -1);
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::Return) {
                    std::string query;
                    {
                        std::lock_guard lock(search_mtx);
                        int             sel = search_state.input_overlay_sel;
                        if (sel >= 0 && sel < static_cast<int>(search_state.vin_list.size()) &&
                            !search_state.vin_list[sel].is_coinbase)
                            query = search_state.vin_list[sel].txid;
                    }
                    if (!query.empty()) {
                        trigger_tx_search(query, false);
                        return true;
                    }
                }
                if (event == Event::Character('q')) {
                    screen.ExitLoopClosure()();
                    return true;
                }
                return false;
            }
        }

        // Peers tab: disconnecting overlay
        if (tab_index == 3 && peer_disconnect_overlay) {
            if (event == Event::Escape && !peer_action_in_flight.load()) {
                peer_disconnect_overlay = false;
                {
                    std::lock_guard lock(peer_action_mtx);
                    peer_action = PeerActionResult{};
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Character('q')) {
                screen.ExitLoopClosure()();
                return true;
            }
            return true; // swallow all other keys while overlay is visible
        }
        // Peers tab: detail overlay mode
        if (tab_index == 3 && peer_detail_open) {
            if (event == Event::Escape) {
                peer_detail_open = false;
                {
                    std::lock_guard lock(peer_action_mtx);
                    peer_action = PeerActionResult{};
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::ArrowLeft || event == Event::ArrowRight ||
                event == Event::ArrowUp || event == Event::ArrowDown) {
                peer_detail_sel = 1 - peer_detail_sel; // toggle 0↔1
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Return || event == Event::Character('d') ||
                event == Event::Character('b')) {
                int action_sel = peer_detail_sel;
                if (event == Event::Character('d'))
                    action_sel = 0;
                else if (event == Event::Character('b'))
                    action_sel = 1;
                std::string addr;
                {
                    std::lock_guard lock(state_mtx);
                    if (peer_selected >= 0 && peer_selected < (int)state.peers.size())
                        addr = state.peers[peer_selected].addr;
                }
                if (!addr.empty()) {
                    if (action_sel == 0) {
                        peer_detail_open        = false;
                        peer_disconnect_overlay = true;
                        trigger_peer_action(addr, false);
                    } else {
                        peer_detail_open        = false;
                        peer_disconnect_overlay = true;
                        trigger_peer_action(addr, true);
                    }
                }
                return true;
            }
            if (event == Event::Character('q')) {
                screen.ExitLoopClosure()();
                return true;
            }
            return false;
        }
        // Add Node overlay input mode
        if (addnode_input_active) {
            if (event == Event::Escape) {
                addnode_input_active = false;
                addnode_str.clear();
                {
                    std::lock_guard lock(addnode_mtx);
                    addnode_state = AddNodeState{};
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            bool addnode_done;
            {
                std::lock_guard lock(addnode_mtx);
                addnode_done = addnode_state.pending || addnode_state.has_result;
            }
            if (!addnode_done) {
                if (event == Event::Return) {
                    std::string addr = trimmed(addnode_str);
                    if (!addr.empty()) {
                        static const char* cmds[] = {"onetry", "add"};
                        AddNodeState       snap;
                        {
                            std::lock_guard lock(addnode_mtx);
                            snap = addnode_state;
                        }
                        trigger_addnode(addr, cmds[snap.cmd_idx]);
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::Backspace) {
                    if (!addnode_str.empty())
                        addnode_str.pop_back();
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowLeft) {
                    {
                        std::lock_guard lock(addnode_mtx);
                        addnode_state.cmd_idx = (addnode_state.cmd_idx + 1) % 2;
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowRight) {
                    {
                        std::lock_guard lock(addnode_mtx);
                        addnode_state.cmd_idx = (addnode_state.cmd_idx + 1) % 2;
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::Tab || event == Event::TabReverse)
                    return true;
                if (event.is_character()) {
                    addnode_str += event.character();
                    screen.PostEvent(Event::Custom);
                    return true;
                }
            }
            return true; // swallow all other keys while overlay is open
        }
        // Ban / Unban Node overlay input mode
        if (ban_input_active) {
            if (event == Event::Escape) {
                ban_input_active = false;
                ban_str.clear();
                {
                    std::lock_guard lock(ban_mtx);
                    ban_state = BanNodeState{};
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            bool ban_done;
            {
                std::lock_guard lock(ban_mtx);
                ban_done = ban_state.pending || ban_state.has_result;
            }
            if (!ban_done) {
                if (event == Event::Return) {
                    std::string addr = trimmed(ban_str);
                    if (!addr.empty()) {
                        bool remove;
                        {
                            std::lock_guard lock(ban_mtx);
                            remove = ban_state.is_remove;
                        }
                        trigger_setban(addr, remove);
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::Backspace) {
                    if (!ban_str.empty())
                        ban_str.pop_back();
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowLeft || event == Event::ArrowRight) {
                    {
                        std::lock_guard lock(ban_mtx);
                        ban_state.is_remove = !ban_state.is_remove;
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::Tab || event == Event::TabReverse)
                    return true;
                if (event.is_character()) {
                    ban_str += event.character();
                    screen.PostEvent(Event::Custom);
                    return true;
                }
            }
            return true; // swallow all other keys while overlay is open
        }
        // Peers tab: Added Nodes overlay
        if (tab_index == 3 && peers_panel == 1) {
            if (event == Event::Escape) {
                peers_panel = 0;
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::ArrowDown || event == Event::ArrowUp) {
                std::lock_guard lock(added_nodes_mtx);
                int             n = static_cast<int>(added_nodes.size());
                if (n > 0) {
                    if (addednodes_sel < 0)
                        addednodes_sel = 0;
                    else if (event == Event::ArrowDown)
                        addednodes_sel = std::min(addednodes_sel + 1, n - 1);
                    else
                        addednodes_sel = std::max(addednodes_sel - 1, 0);
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Return && addednodes_sel >= 0) {
                std::string addr;
                {
                    std::lock_guard lock(added_nodes_mtx);
                    if (addednodes_sel < static_cast<int>(added_nodes.size()))
                        addr = added_nodes[addednodes_sel].addednode;
                }
                if (!addr.empty()) {
                    do_remove_added_node(addr);
                    addednodes_sel = -1;
                }
                return true;
            }
            if (event == Event::Character('a')) {
                addnode_input_active = true;
                addnode_str.clear();
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Character('q')) {
                screen.ExitLoopClosure()();
                return true;
            }
            return false;
        }
        // Peers tab: Ban List overlay
        if (tab_index == 3 && peers_panel == 2) {
            if (event == Event::Escape) {
                peers_panel = 0;
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::ArrowDown || event == Event::ArrowUp) {
                std::lock_guard lock(banned_list_mtx);
                int             n = static_cast<int>(banned_list.size());
                if (n > 0) {
                    if (banlist_sel < 0)
                        banlist_sel = 0;
                    else if (event == Event::ArrowDown)
                        banlist_sel = std::min(banlist_sel + 1, n - 1);
                    else
                        banlist_sel = std::max(banlist_sel - 1, 0);
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Return && banlist_sel >= 0) {
                std::string addr;
                {
                    std::lock_guard lock(banned_list_mtx);
                    if (banlist_sel < static_cast<int>(banned_list.size()))
                        addr = banned_list[banlist_sel].address;
                }
                if (!addr.empty()) {
                    do_unban(addr);
                    banlist_sel = -1;
                }
                return true;
            }
            if (event == Event::Character('q')) {
                screen.ExitLoopClosure()();
                return true;
            }
            return false;
        }
        // Peers tab: list navigation
        if (tab_index == 3) {
            if (event == Event::ArrowDown || event == Event::ArrowUp) {
                int n = static_cast<int>(state.peers.size());
                if (n > 0) {
                    if (event == Event::ArrowDown)
                        peer_selected = std::min(peer_selected + 1, n - 1);
                    else
                        peer_selected = std::max(peer_selected - 1, 0);
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Return && peer_selected >= 0) {
                peer_detail_open = true;
                peer_detail_sel  = 0;
                {
                    std::lock_guard lock(peer_action_mtx);
                    peer_action = PeerActionResult{};
                }
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Character('a')) {
                peers_panel    = 1;
                addednodes_sel = -1;
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Character('b')) {
                peers_panel = 2;
                banlist_sel = -1;
                screen.PostEvent(Event::Custom);
                return true;
            }
        }
        // Explorer tab navigation
        if (tab_index == 1) {
            // Reset to unfocused when switching into the Explorer tab from another tab.
            if (prev_tab_index != 1) {
                mempool_pane   = -1;
                mempool_tx_sel = -1;
            }
            prev_tab_index = 1;

            // Check whether a search result overlay is showing.
            bool has_overlay;
            {
                std::lock_guard lock(search_mtx);
                has_overlay = !search_state.txid.empty();
            }

            if (has_overlay) {
                // Block L/R from switching tabs while overlay is open.
                if (event == Event::ArrowLeft || event == Event::ArrowRight)
                    return true;
            } else if (mempool_pane == 1) {
                // ── Tx list pane navigation ─────────────────────────────────
                int ntx;
                {
                    std::lock_guard lock(browse_mtx);
                    const auto&     bs = (mempool_sel == 0) ? browse_mempool_ss : browse_block_ss;
                    ntx                = static_cast<int>(bs.blk_tx_list.size());
                }
                if (ntx == 0)
                    return true; // nothing to navigate (shouldn't happen)
                if (event == Event::ArrowDown) {
                    mempool_tx_sel = std::min(mempool_tx_sel + 1, ntx - 1);
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::ArrowUp) {
                    if (mempool_tx_sel <= 0)
                        mempool_pane = 0;
                    else
                        mempool_tx_sel--;
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::PageDown) {
                    mempool_tx_sel = std::min(mempool_tx_sel + explorer_win_size, ntx - 1);
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::PageUp) {
                    mempool_tx_sel = std::max(mempool_tx_sel - explorer_win_size, 0);
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                if (event == Event::Return && mempool_tx_sel >= 0 && mempool_tx_sel < ntx) {
                    std::string txid, blkhash;
                    {
                        std::lock_guard lock(browse_mtx);
                        const auto& bs = (mempool_sel == 0) ? browse_mempool_ss : browse_block_ss;
                        txid           = bs.blk_tx_list[mempool_tx_sel].txid;
                        blkhash        = bs.blk_hash; // empty for mempool txs
                    }
                    if (!txid.empty())
                        trigger_tx_search(txid, false, blkhash);
                    return true;
                }
                if (event == Event::Escape) {
                    mempool_pane   = 0;
                    mempool_tx_sel = -1;
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                // Block L/R from switching tabs while in tx list.
                if (event == Event::ArrowLeft || event == Event::ArrowRight)
                    return true;
            } else {
                // ── Blocks pane navigation ──────────────────────────────────
                bool browse_loaded;
                {
                    std::lock_guard lock(browse_mtx);
                    const auto&     bs = (mempool_sel == 0) ? browse_mempool_ss : browse_block_ss;
                    browse_loaded      = bs.found;
                }
                // On Custom events: restore mempool selection after refresh,
                // then auto-trigger block/mempool load if needed.
                if (event == Event::Custom) {
                    if (mempool_restore_idx >= 0 || !mempool_restore_txid.empty()) {
                        std::lock_guard lock(browse_mtx);
                        if (browse_mempool_ss.is_mempool && browse_mempool_ss.found) {
                            const auto& list = browse_mempool_ss.blk_tx_list;
                            int         n    = static_cast<int>(list.size());
                            int         sel  = -1;
                            for (int i = 0; i < n; ++i) {
                                if (list[i].txid == mempool_restore_txid) {
                                    sel = i;
                                    break;
                                }
                            }
                            if (sel < 0 && mempool_restore_idx >= 0 && n > 0)
                                sel = std::min(mempool_restore_idx, n - 1);
                            if (sel >= 0)
                                mempool_tx_sel = sel;
                            mempool_restore_txid.clear();
                            mempool_restore_idx = -1;
                        }
                    }
                    trigger_browse_if_needed();
                    return false; // don't consume — let render proceed
                }
                // ↓ focus blocks pane (unfocused→focused) or enter tx list.
                if (event == Event::ArrowDown) {
                    if (mempool_pane == -1) {
                        mempool_pane = 0;
                        trigger_browse_if_needed();
                    } else if (browse_loaded) {
                        mempool_pane   = 1;
                        mempool_tx_sel = 0;
                    }
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                // ↑ when blocks pane focused, unfocus back to top level.
                if (mempool_pane == 0 && event == Event::ArrowUp) {
                    mempool_pane = -1;
                    screen.PostEvent(Event::Custom);
                    return true;
                }
                // ←/→ navigate blocks — only when blocks pane has focus.
                if (mempool_pane == 0 &&
                    (event == Event::ArrowLeft || event == Event::ArrowRight)) {
                    int n;
                    {
                        std::lock_guard lock(state_mtx);
                        n = static_cast<int>(state.recent_blocks.size());
                    }
                    // TODO: allow scrolling the block bar when there are more
                    // blocks than fit on screen, instead of clamping here.
                    int max_visible = std::max(2, (Terminal::Size().dimx - 4) / 11) - 1;
                    int max_sel     = std::min(n, max_visible);
                    int delta       = (event == Event::ArrowLeft) ? -1 : 1;
                    mempool_sel     = std::clamp(mempool_sel + delta, 0, max_sel);
                    mempool_tx_sel  = -1;
                    trigger_browse_if_needed();
                    screen.PostEvent(Event::Custom);
                    return true;
                }
            }
        } else {
            prev_tab_index = tab_index;
        }
        // Normal mode
        if (event == Event::Character('/')) {
            global_search_active = true;
            global_search_str.clear();
            screen.PostEvent(Event::Custom);
            return true;
        }
        // Tools tab keys
        if (tab_index == 4) {
            // Compute shutdown row index: after broadcast (0) + optional result txid (1)
            bool has_result_row;
            {
                std::lock_guard lock(broadcast_mtx);
                has_result_row = broadcast_state.has_result && broadcast_state.success;
            }
            int shutdown_idx = 1 + (has_result_row ? 1 : 0);

            auto do_shutdown = [&]() {
                try {
                    RpcClient rpc(cfg, auth);
                    rpc.call("stop", {});
                } catch (...) { // NOLINT(bugprone-empty-catch)
                }
                screen.ExitLoopClosure()();
            };
            if (event == Event::Character('b')) {
                open_broadcast_dialog();
                return true;
            }
            if (event == Event::Character('Q')) {
                do_shutdown();
                return true;
            }
            if (event == Event::Return) {
                if (tools_sel == 0) {
                    open_broadcast_dialog();
                } else if (tools_sel == shutdown_idx) {
                    do_shutdown();
                } else if (tools_sel == 1 && has_result_row) {
                    std::string txid;
                    {
                        std::lock_guard lock(broadcast_mtx);
                        if (broadcast_state.has_result && broadcast_state.success)
                            txid = broadcast_state.result_txid;
                    }
                    if (!txid.empty())
                        trigger_tx_search(txid, true);
                }
                return true;
            }
            if (event == Event::ArrowDown || event == Event::ArrowUp) {
                if (event == Event::ArrowDown)
                    tools_sel = std::min(tools_sel + 1, shutdown_idx);
                else
                    tools_sel = std::max(tools_sel - 1, 0);
                screen.PostEvent(Event::Custom);
                return true;
            }
        }
        if (event == Event::ArrowDown || event == Event::ArrowUp) {
            bool handled = false;
            {
                std::lock_guard lock(search_mtx);
                if (search_state.found && search_state.confirmed && !search_state.is_block) {
                    int max_sel = io_max_sel(search_state);
                    if (event == Event::ArrowDown)
                        search_state.io_selected = std::min(search_state.io_selected + 1, max_sel);
                    else
                        search_state.io_selected = std::max(search_state.io_selected - 1, -1);
                    handled = true;
                }
            }
            if (handled) {
                screen.PostEvent(Event::Custom);
                return true;
            }
        }
        if (event == Event::Return) {
            // io_selected: 0/-1=view block, 1=inputs overlay, 2=outputs overlay
            bool        open_inputs = false, open_outputs = false;
            std::string query;
            {
                std::lock_guard lock(search_mtx);
                if (search_state.found && search_state.confirmed && !search_state.is_block) {
                    int sel         = search_state.io_selected;
                    int inputs_idx  = io_inputs_idx(search_state);
                    int outputs_idx = io_outputs_idx(search_state);
                    if (sel == inputs_idx && inputs_idx >= 0) {
                        search_state.inputs_overlay_open = true;
                        search_state.input_overlay_sel   = -1;
                        open_inputs                      = true;
                    } else if (sel == outputs_idx && outputs_idx >= 0) {
                        search_state.outputs_overlay_open = true;
                        search_state.output_overlay_sel   = -1;
                        open_outputs                      = true;
                    } else {
                        query = search_state.blockhash;
                    }
                }
            }
            if (open_inputs || open_outputs) {
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (!query.empty()) {
                trigger_tx_search(query, false);
                return true;
            }
        }
        if (event == Event::Escape) {
            // Pop history first, then dismiss overlay, then quit
            bool had_overlay = false;
            {
                std::lock_guard lock(search_mtx);
                if (!search_history.empty()) {
                    search_state = search_history.back();
                    search_history.pop_back();
                    had_overlay = true;
                } else if (!search_state.txid.empty()) {
                    search_state = TxSearchState{};
                    had_overlay  = true;
                }
            }
            if (had_overlay) {
                screen.PostEvent(Event::Custom);
                return true;
            }
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character('q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    // Render callback: wakes the UI between the two poll phases so core data
    // appears immediately without waiting for the slower getblockstats calls.
    auto wake_screen = [&] { screen.PostEvent(Event::Custom); };

    // Background polling thread
    std::thread poll_thread([&] {
        RpcClient rpc(cfg, auth);

        // Initial fetch immediately
        {
            std::lock_guard lock(state_mtx);
            state.refreshing = true;
        }
        screen.PostEvent(Event::Custom);

        poll_rpc(rpc, state, state_mtx, wake_screen);

        {
            std::lock_guard lock(state_mtx);
            state.refreshing = false;
        }
        screen.PostEvent(Event::Custom);

        // Periodic refresh
        while (running) {
            // Sleep in small increments so we can exit promptly
            for (int i = 0; i < refresh_secs * 10 && running; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!running)
                break;

            {
                std::lock_guard lock(state_mtx);
                state.refreshing = true;
            }
            screen.PostEvent(Event::Custom);

            // Re-read cookie when disconnected (bitcoind restart creates a new one).
            if (!explicit_creds) {
                std::lock_guard lock(state_mtx);
                if (!state.connected) {
                    std::string path =
                        cookie_file.empty() ? cookie_path(network, datadir) : cookie_file;
                    auth.update([&](auto& a) {
                        try {
                            apply_cookie(a, path);
                            rpc = RpcClient(cfg, a);
                        } catch (...) { // NOLINT(bugprone-empty-catch)
                        }
                    });
                }
            }

            poll_rpc(rpc, state, state_mtx, wake_screen);

            {
                std::lock_guard lock(state_mtx);
                state.refreshing = false;
            }
            screen.PostEvent(Event::Custom);
        }
    });

    // Animation ticker: advances frame counter at ~25 fps while animation is active.
    std::thread anim_thread([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            if (!running)
                break;
            bool needs_redraw = false;
            {
                std::lock_guard lock(state_mtx);
                if (state.block_anim_active) {
                    state.block_anim_frame++;
                    if (state.block_anim_frame >= BLOCK_ANIM_TOTAL_FRAMES)
                        state.block_anim_active = false;
                    needs_redraw = true;
                }
            }
            if (needs_redraw)
                screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(event_handler);

    running = false;
    if (search_thread.joinable())
        search_thread.join();
    if (browse_thread.joinable())
        browse_thread.join();
    if (broadcast_thread.joinable())
        broadcast_thread.join();
    if (addnode_thread.joinable())
        addnode_thread.join();
    if (ban_thread.joinable())
        ban_thread.join();
    if (peer_action_thread.joinable())
        peer_action_thread.join();
    if (remove_addednode_thread.joinable())
        remove_addednode_thread.join();
    if (unban_action_thread.joinable())
        unban_action_thread.join();
    if (added_nodes_thread.joinable())
        added_nodes_thread.join();
    if (banned_list_thread.joinable())
        banned_list_thread.join();
    if (softforks_thread.joinable())
        softforks_thread.join();
    if (launch_thread.joinable())
        launch_thread.join();
    poll_thread.join();
    anim_thread.join();

    return 0;
}

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bitcoin-tui: %s\n", e.what());
        return 1;
    }
}
