#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "bitcoind.hpp"
#include "format.hpp"
#include "guarded.hpp"
#include "poll.hpp"
#include "render.hpp"
#include "rpc_client.hpp"
#include "state.hpp"
#include "tabs/dashboard.hpp"
#include "tabs/mempool.hpp"
#include "tabs/network.hpp"
#include "tabs/peers.hpp"
#include "tabs/slowblocks.hpp"
#include "tabs/tools.hpp"

// ============================================================================
// Cookie authentication helpers
// ============================================================================

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

static std::string network_subdir(const std::string& network) {
    if (network == "testnet3")
        return "testnet3/";
    if (network == "signet")
        return "signet/";
    if (network == "regtest")
        return "regtest/";
    return "";
}

static std::string cookie_path(const std::string& network, const std::string& datadir) {
    return datadir + "/" + network_subdir(network) + ".cookie";
}

static void apply_cookie(RpcAuth& auth, const std::string& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open cookie file: " + path);
    std::string line;
    if (!std::getline(f, line) || line.empty())
        throw std::runtime_error("Cookie file is empty: " + path);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    auto colon = line.find(':');
    if (colon == std::string::npos)
        throw std::runtime_error("Invalid cookie file (no ':' found): " + path);
    auth.user     = line.substr(0, colon);
    auth.password = line.substr(colon + 1);
}

// ============================================================================
// Application entry point
// ============================================================================
namespace {
class Application {
  private:
    /* Thread safety:
     *  - all variables shared across threads are declared here
     *  - read-only variables are set in configure() before
     *    threads start, then treated as const in run()
     *  - read-write variables are either `mutable atomic`
     *    or `mutable Guarded` to ensure safety
     */

    RpcConfig                cfg;
    mutable Guarded<RpcAuth> auth;
    int                      refresh_secs = 5;
    std::string              network      = "main";
    std::string              cookie_file;
    std::string              datadir;
    bool                     explicit_creds = false;
    std::string              bitcoind_cmd;
    bool                     explicit_host = false;
    bool                     can_launch    = false;
    std::string              debug_log_file;

    // Shared state
    mutable Guarded<AppState> state;
    mutable std::atomic<bool> running{false};

    // Connection overlay state (not a tab — shown when disconnected)
    mutable std::atomic<bool>                 launch_in_flight{false};
    mutable std::atomic<bool>                 launch_done{false};
    mutable std::atomic<int>                  launch_exit_code{0};
    mutable Guarded<std::vector<std::string>> launch_output;
    mutable std::thread                       launch_thread;
    mutable std::atomic<int>                  conn_overlay_sel{0};

    // Configure from command line
    int configure(int argc, char* argv[]);

    // Run the application with thread-safe state
    int run() const;

  public:
    static int run(int argc, char* argv[]) {
        Application app;
        int         rc = app.configure(argc, argv);
        if (rc != 0)
            return rc;
        return app.run();
    }
};

int Application::configure(int argc, char* argv[]) {
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
        } else if (arg == "--port" || arg == "-p") {
            cfg.port = std::stoi(next());
        } else if (arg == "--user" || arg == "-u") {
            auth.update([&](auto& a) { a.user = next(); });
            explicit_creds = true;
        } else if (arg == "--password" || arg == "-P") {
            auth.update([&](auto& a) { a.password = next(); });
            explicit_creds = true;
        } else if (arg == "--cookie" || arg == "-c") {
            cookie_file = next();
        } else if (arg == "--datadir" || arg == "-d") {
            datadir = next();
        } else if (arg == "--refresh" || arg == "-r") {
            refresh_secs = std::stoi(next());
        } else if (arg == "--testnet") {
            cfg.port = 18332;
            network  = "testnet3";
        } else if (arg == "--regtest") {
            cfg.port = 18443;
            network  = "regtest";
        } else if (arg == "--signet") {
            cfg.port = 38332;
            network  = "signet";
        } else if (arg == "--debuglog") {
            debug_log_file = next();
        } else if (arg == "--bitcoind") {
            bitcoind_cmd = next();
        } else if (arg == "--version" || arg == "-v") {
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
                "      --debuglog <path>  Path to debug.log         (default: <datadir>/debug.log)\n"
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

    if (datadir.empty())
        datadir = default_datadir();

    if (!explicit_creds) {
        std::string path = cookie_file.empty() ? cookie_path(network, datadir) : cookie_file;
        try {
            auth.update([&](auto& a) { apply_cookie(a, path); });
        } catch (const std::exception& e) {
            if (!cookie_file.empty()) {
                std::fprintf(stderr, "bitcoin-tui: %s\n", e.what());
                return 1;
            }
        }
    }

    if (!bitcoind_cmd.empty()) {
        can_launch = true;
    } else if (!explicit_host) {
        bitcoind_cmd = find_bitcoind();
        can_launch   = !bitcoind_cmd.empty();
    }
    return 0;
}

int Application::run() const {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    running     = true;

    // Global search bar state
    std::string global_search_str;
    bool        global_search_active = false;

    // Tab toggle
    std::vector<std::string> tab_labels = {"Dashboard", "Mempool", "Network", "Peers", "Tools", "Slow Blocks"};
    int                      tab_index  = 0;
    auto                     tab_toggle = Toggle(&tab_labels, &tab_index);

    // Tab objects (mempool first — tools captures a reference to it via lambda)
    DashboardTab dashboard_tab(cfg, auth, screen, running, state, refresh_secs);
    MempoolTab   mempool_tab(cfg, auth, screen, running, state, refresh_secs);
    NetworkTab   network_tab(cfg, auth, screen, running, state, refresh_secs);
    PeersTab     peers_tab(cfg, auth, screen, running, state, refresh_secs);
    ToolsTab     tools_tab(
        cfg, auth, screen, running, state, refresh_secs,
        [&](const std::string& q, bool sw) { mempool_tab.trigger_search(q, sw, tab_index); });

    std::string debug_log = debug_log_file.empty()
                               ? datadir + "/" + network_subdir(network) + "debug.log"
                               : debug_log_file;
    SlowBlocksTab slowblocks_tab(cfg, auth, screen, running, state, refresh_secs, debug_log);

    std::vector<Tab*> tabs = {&dashboard_tab, &mempool_tab, &network_tab, &peers_tab, &tools_tab, &slowblocks_tab};

    auto layout = Container::Vertical({tab_toggle});

    auto renderer = Renderer(layout, [&]() -> Element {
        AppState snap = state.get();

        Element tab_content = (tab_index < 0 || tab_index >= tabs.size())
                                  ? text("Unknown tab")
                                  : tabs[tab_index]->render(snap);

        // Status bar — left side
        Element status_left;
        if (!snap.connected && !snap.error_message.empty()) {
            status_left = hbox({
                text(" ERROR ") | bgcolor(Color::Red) | color(Color::White) | bold,
                text(" " + snap.error_message) | color(Color::Red),
            });
        } else {
            status_left = hbox({
                text(" "),
                snap.connected ? text("\u25cf CONNECTED") | color(Color::Green) | bold
                               : text("\u25cb CONNECTING\u2026") | color(Color::Yellow) | bold,
                text("  Last update: " + snap.last_update) | color(Color::GrayDark),
            });
        }

        // Status bar — right side (context-sensitive hints)
        Element status_right =
            global_search_active
                ? hbox({text("  [Enter] search  [Esc] cancel ") | color(Color::Yellow)})
                : tabs[tab_index]->key_hints(snap);

        // Connection overlay (shown when disconnected)
        auto content = snap.connected ? tab_content | flex : [&]() -> Element {
            Elements conn_rows;
            conn_rows.push_back(hbox({text(" Network : ") | color(Color::GrayDark),
                                      text(network) | color(Color::White)}));
            conn_rows.push_back(
                hbox({text(" RPC port: ") | color(Color::GrayDark),
                      text(cfg.host + ":" + std::to_string(cfg.port)) | color(Color::White)}));
            conn_rows.push_back(hbox({text(" Datadir : ") | color(Color::GrayDark),
                                      text(datadir) | color(Color::White)}));
            conn_rows.push_back(
                hbox({text(" Auth    : ") | color(Color::GrayDark),
                      text(explicit_creds ? auth.get().user + ":<hidden>"
                                          : "cookie (" + cookie_path(network, datadir) + ")") |
                          color(Color::White)}));
            conn_rows.push_back([&]() -> Element {
                if (launch_in_flight.load() || launch_done.load()) {
                    Elements rows;
                    rows.push_back(text(launch_in_flight.load() ? "  Launching bitcoind\u2026"
                                        : launch_exit_code == 0 ? "  \u2713 bitcoind started"
                                                                : "  \u2717 bitcoind failed") |
                                   color(launch_in_flight.load() ? Color::Yellow
                                         : launch_exit_code == 0 ? Color::Green
                                                                 : Color::Red) |
                                   bold);
                    launch_output.access([&](const auto& lines) {
                        for (const auto& line : lines)
                            rows.push_back(paragraph("  " + line) | color(Color::White));
                    });
                    return vbox(std::move(rows));
                }
                return hbox({text(" Error   : ") | color(Color::GrayDark),
                             paragraph(snap.error_message.empty() ? std::string("connecting\u2026")
                                                                  : snap.error_message) |
                                 color(Color::Yellow)});
            }());
            conn_rows.push_back(separator());
            conn_rows.push_back(hbox({
                text("  "),
                can_launch
                    ? text(" Launch bitcoind ") |
                          (!launch_in_flight.load() && !launch_done.load() && conn_overlay_sel == 0
                               ? inverted
                               : dim)
                    : text(""),
                filler(),
                text(" Quit ") |
                    (!launch_in_flight.load() && conn_overlay_sel == (can_launch ? 1 : 0) ? inverted
                                                                                          : dim),
                text("  "),
            }));

            auto panel = build_titled_panel(" Connection Failed - Reconnecting ", "",
                                            std::move(conn_rows), 80, Color::Red);
            return center_overlay(std::move(panel));
        }();

        return vbox({
            // Title bar
            hbox({
                text(" \u20bf Bitcoin Core TUI ") | bold | color(Color::Gold1),
                text(" " + cfg.host + ":" + std::to_string(cfg.port) + " ") |
                    color(Color::GrayDark),
                filler(),
                snap.chain.empty() || snap.chain == "\u2014"
                    ? text("")
                    : text(" " + snap.chain + " ") | bold |
                          (snap.chain == "main" ? bgcolor(Color::DarkGreen)
                                                : bgcolor(Color::Yellow)) |
                          (snap.chain == "main" ? color(Color::White) : color(Color::Black)),
            }) | border,

            // Tab bar with global search
            hbox({
                [&] {
                    Elements tabs;
                    for (int i = 0; i < static_cast<int>(tab_labels.size()); ++i) {
                        if (i > 0)
                            tabs.push_back(text("\u2502") | automerge);
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
                    constexpr int kWidth    = 46;
                    constexpr int kTextCols = kWidth - 3;
                    std::string   vis       = global_search_str;
                    if (static_cast<int>(vis.size()) > kTextCols)
                        vis = vis.substr(vis.size() - kTextCols);
                    return hbox({text(" "), text(vis) | color(Color::White),
                                 text("\u2502") | color(Color::White), filler()}) |
                           size(WIDTH, EQUAL, kWidth);
                }(),
            }) | border,

            // Content
            content,

            // Status bar
            hbox({status_left, filler(), status_right}) | border,
        });
    });

    // Event handler — ordering matches the critical constraint documented in memory:
    // global_search → tools_input → tx overlays → addnode_input → ban_input
    // → tab3 nav → mempool nav → normal keys
    auto event_handler = CatchEvent(renderer, [&](const Event& event) -> bool {
        // Connection overlay: consumes all events when disconnected
        {
            bool connected = state.access([](const auto& s) { return s.connected; });
            if (connected) {
                launch_done = false;
            } else {
                int max_sel = can_launch ? 1 : 0;
                if (event == Event::Escape || event == Event::Character('q'))
                    screen.ExitLoopClosure()();
                if (event == Event::Return) {
                    if (can_launch && conn_overlay_sel == 0 && !launch_in_flight.load() &&
                        !launch_done.load()) {
                        launch_in_flight = true;
                        launch_done      = false;
                        launch_output.update([](auto& v) { v.clear(); });
                        if (launch_thread.joinable())
                            launch_thread.join();
                        launch_thread = std::thread([&] {
                            launch_exit_code = launch_bitcoind(
                                bitcoind_cmd, datadir, network, [&](const std::string& line) {
                                    launch_output.update([&](auto& v) { v.push_back(line); });
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

        // Global search bar
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
                    mempool_tab.trigger_search(q, true, tab_index);
                screen.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Backspace) {
                if (!global_search_str.empty())
                    global_search_str.pop_back();
                screen.PostEvent(Event::Custom);
                return true;
            }
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

        // Tab-specific event dispatch (priority order — see MEMORY.md CatchEvent note)
        if (tools_tab.handle_tools_input(event))
            return true;
        if (auto r = mempool_tab.handle_tx_overlay(event); r.has_value())
            return *r;
        if (peers_tab.handle_addnode_input(event))
            return true;
        if (peers_tab.handle_ban_input(event))
            return true;
        if (tab_index == 3 && peers_tab.handle_tab_events(event))
            return true;
        if (tab_index == 1 && mempool_tab.handle_navigation(event))
            return true;

        // Normal mode keys
        if (event == Event::Character('/')) {
            global_search_active = true;
            global_search_str.clear();
            screen.PostEvent(Event::Custom);
            return true;
        }
        if (tab_index == 4 && tools_tab.handle_keys(event))
            return true;
        if (mempool_tab.handle_io_nav(event))
            return true;
        if (mempool_tab.handle_enter(event))
            return true;
        if (event == Event::Escape) {
            if (mempool_tab.handle_escape(event))
                return true;
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character('q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    auto wake_screen = [&] { screen.PostEvent(Event::Custom); };

    // Background polling thread
    std::thread poll_thread([&] {
        RpcClient rpc(cfg, auth);

        state.update([](auto& s) { s.refreshing = true; });
        screen.PostEvent(Event::Custom);

        poll_rpc(rpc, state, wake_screen);

        state.update([](auto& s) { s.refreshing = false; });
        screen.PostEvent(Event::Custom);

        while (running) {
            for (int i = 0; i < refresh_secs * 10 && running; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!running)
                break;

            state.update([](auto& s) { s.refreshing = true; });
            screen.PostEvent(Event::Custom);

            if (!explicit_creds) {
                bool disconnected = state.access([](const auto& s) { return !s.connected; });
                if (disconnected) {
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

            poll_rpc(rpc, state, wake_screen);

            state.update([](auto& s) { s.refreshing = false; });
            screen.PostEvent(Event::Custom);
        }
    });

    // Animation ticker
    std::thread anim_thread([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            if (!running)
                break;
            bool needs_redraw = state.update([](auto& s) {
                if (s.block_anim_active) {
                    s.block_anim_frame++;
                    if (s.block_anim_frame >= BLOCK_ANIM_TOTAL_FRAMES)
                        s.block_anim_active = false;
                    return true;
                }
                return false;
            });
            if (needs_redraw)
                screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(event_handler);

    running = false;
    for (auto tab : tabs) {
        tab->join();
    }
    if (launch_thread.joinable())
        launch_thread.join();
    poll_thread.join();
    anim_thread.join();

    return 0;
}
} // anonymous namespace

int main(int argc, char* argv[]) {
    try {
        return Application::run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bitcoin-tui: %s\n", e.what());
        return 1;
    }
}
