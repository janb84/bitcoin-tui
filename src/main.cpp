#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
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

#include "rpc_client.hpp"

using namespace ftxui;

// ============================================================================
// Application state (shared between render thread and RPC polling thread)
// ============================================================================
struct PeerInfo {
    int         id = 0;
    std::string addr;
    std::string network;
    std::string subver;
    bool        inbound       = false;
    int64_t     bytes_sent    = 0;
    int64_t     bytes_recv    = 0;
    double      ping_ms       = -1.0;
    int         version       = 0;
    int64_t     synced_blocks = 0;
};

struct AppState {
    // Blockchain
    std::string chain      = "—";
    int64_t     blocks     = 0;
    int64_t     headers    = 0;
    double      difficulty = 0.0;
    double      progress   = 0.0;
    bool        pruned     = false;
    bool        ibd        = false;
    std::string bestblockhash;

    // Network
    int         connections     = 0;
    int         connections_in  = 0;
    int         connections_out = 0;
    std::string subversion;
    int         protocol_version = 0;
    bool        network_active   = true;
    double      relay_fee        = 0.0;

    // Mempool
    int64_t mempool_tx      = 0;
    int64_t mempool_bytes   = 0;
    int64_t mempool_usage   = 0;
    int64_t mempool_max     = 300000000;
    double  mempool_min_fee = 0.0;
    double  total_fee       = 0.0;

    // Mining
    double network_hashps = 0.0;

    // Peers
    std::vector<PeerInfo> peers;

    // Status
    std::string last_update;
    std::string error_message;
    bool        connected  = false;
    bool        refreshing = false;
};

// ============================================================================
// Formatting helpers
// ============================================================================
static std::string fmt_int(int64_t n) {
    bool        negative = n < 0;
    std::string s        = std::to_string(std::abs(n));
    int         pos      = static_cast<int>(s.size()) - 3;
    while (pos > 0) {
        s.insert(static_cast<size_t>(pos), ",");
        pos -= 3;
    }
    return negative ? "-" + s : s;
}

static std::string fmt_bytes(int64_t b) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    if (b >= 1'000'000'000LL)
        ss << static_cast<double>(b) / 1e9 << " GB";
    else if (b >= 1'000'000LL)
        ss << static_cast<double>(b) / 1e6 << " MB";
    else if (b >= 1'000LL)
        ss << static_cast<double>(b) / 1e3 << " KB";
    else
        ss << b << " B";
    return ss.str();
}

static std::string fmt_difficulty(double d) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (d >= 1e18)
        ss << d / 1e18 << " E";
    else if (d >= 1e15)
        ss << d / 1e15 << " P";
    else if (d >= 1e12)
        ss << d / 1e12 << " T";
    else if (d >= 1e9)
        ss << d / 1e9 << " G";
    else
        ss << d;
    return ss.str();
}

static std::string fmt_hashrate(double h) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (h >= 1e18)
        ss << h / 1e18 << " EH/s";
    else if (h >= 1e15)
        ss << h / 1e15 << " PH/s";
    else if (h >= 1e12)
        ss << h / 1e12 << " TH/s";
    else if (h >= 1e9)
        ss << h / 1e9 << " GH/s";
    else if (h >= 1e6)
        ss << h / 1e6 << " MH/s";
    else if (h >= 1e3)
        ss << h / 1e3 << " kH/s";
    else
        ss << h << " H/s";
    return ss.str();
}

static std::string fmt_satsvb(double btc_per_kvb) {
    double             sats_per_vb = btc_per_kvb * 1e5; // BTC/kvB → sat/vB
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << sats_per_vb << " sat/vB";
    return ss.str();
}

static std::string now_string() {
    auto               t  = std::time(nullptr);
    auto               tm = *std::localtime(&t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ============================================================================
// Tab renderers
// ============================================================================
static Element label_value(const std::string& lbl, const std::string& val,
                           Color val_color = Color::Default) {
    return hbox({
        text(lbl) | color(Color::GrayDark),
        text(val) | color(val_color) | bold,
    });
}

static Element section_box(const std::string& title, Elements rows) {
    Elements content;
    content.reserve(rows.size() + 1);
    content.push_back(text(" " + title + " ") | bold | color(Color::Gold1));
    for (auto& r : rows)
        content.push_back(std::move(r));
    return vbox(std::move(content)) | border;
}

// --- Dashboard --------------------------------------------------------------
static Element render_dashboard(const AppState& s) {
    // Chain section
    std::string chain_color_name = s.chain == "main" ? "mainnet" : s.chain;
    Color       chain_color      = (s.chain == "main") ? Color::Green : Color::Yellow;

    auto blockchain_section = section_box(
        "Blockchain",
        {
            label_value("  Chain       : ", chain_color_name, chain_color),
            label_value("  Height      : ", fmt_int(s.blocks)),
            label_value("  Headers     : ", fmt_int(s.headers)),
            label_value("  Difficulty  : ", fmt_difficulty(s.difficulty)),
            label_value("  Hash Rate   : ", fmt_hashrate(s.network_hashps)),
            hbox({
                text("  Sync        : ") | color(Color::GrayDark),
                gauge(static_cast<float>(s.progress)) | flex |
                    color(s.progress >= 1.0 ? Color::Green : Color::Yellow),
                text(" " + std::to_string(static_cast<int>(s.progress * 100)) + "%") | bold,
            }),
            label_value("  IBD         : ", s.ibd ? "yes" : "no",
                        s.ibd ? Color::Yellow : Color::Green),
            label_value("  Pruned      : ", s.pruned ? "yes" : "no"),
        });

    // Network section
    Color net_color       = s.network_active ? Color::Green : Color::Red;
    auto  network_section = section_box(
        "Network", {
                       label_value("  Active      : ", s.network_active ? "yes" : "no", net_color),
                       label_value("  Connections : ", std::to_string(s.connections)),
                       label_value("    In        : ", std::to_string(s.connections_in)),
                       label_value("    Out       : ", std::to_string(s.connections_out)),
                       label_value("  Client      : ", s.subversion),
                       label_value("  Protocol    : ", std::to_string(s.protocol_version)),
                       label_value("  Relay fee   : ", fmt_satsvb(s.relay_fee)),
                   });

    // Mempool section
    double usage_frac      = s.mempool_max > 0 ? static_cast<double>(s.mempool_usage) /
                                                static_cast<double>(s.mempool_max)
                                               : 0.0;
    auto   mempool_section = section_box(
        "Mempool",
        {
            label_value("  Transactions: ", fmt_int(s.mempool_tx)),
            label_value("  Size        : ", fmt_bytes(s.mempool_bytes)),
            label_value("  Total fee   : ",
                          [&] {
                            std::ostringstream ss;
                            ss << std::fixed << std::setprecision(4) << s.total_fee << " BTC";
                            return ss.str();
                        }()),
            label_value("  Min fee     : ", fmt_satsvb(s.mempool_min_fee)),
            hbox({
                text("  Memory      : ") | color(Color::GrayDark),
                gauge(static_cast<float>(usage_frac)) | flex |
                    color(usage_frac > 0.8 ? Color::Red : Color::Cyan),
                text(" " + fmt_bytes(s.mempool_usage) + " / " + fmt_bytes(s.mempool_max)) | bold,
            }),
        });

    return vbox({
               hbox({
                   blockchain_section | flex,
                   network_section | flex,
               }),
               mempool_section,
           }) |
           flex;
}

// --- Mempool ----------------------------------------------------------------
static Element render_mempool(const AppState& s) {
    double usage_frac  = s.mempool_max > 0 ? static_cast<double>(s.mempool_usage) /
                                                static_cast<double>(s.mempool_max)
                                           : 0.0;
    Color  usage_color = usage_frac > 0.8   ? Color::Red
                         : usage_frac > 0.5 ? Color::Yellow
                                            : Color::Cyan;

    return vbox({
               section_box(
                   "Mempool Details",
                   {
                       label_value("  Transactions    : ", fmt_int(s.mempool_tx)),
                       label_value("  Virtual size    : ", fmt_bytes(s.mempool_bytes)),
                       label_value("  Total fees      : ",
                                   [&] {
                                       std::ostringstream ss;
                                       ss << std::fixed << std::setprecision(8) << s.total_fee
                                          << " BTC";
                                       return ss.str();
                                   }()),
                       label_value("  Min relay fee   : ", fmt_satsvb(s.mempool_min_fee)),
                       separator(),
                       text("  Memory usage") | color(Color::GrayDark),
                       hbox({
                           text("  "),
                           gauge(static_cast<float>(usage_frac)) | flex | color(usage_color),
                           text("  "),
                       }),
                       hbox({
                           text("  Used : ") | color(Color::GrayDark),
                           text(fmt_bytes(s.mempool_usage)) | bold,
                           text("  /  Max : ") | color(Color::GrayDark),
                           text(fmt_bytes(s.mempool_max)) | bold,
                       }),
                   }),
               filler(),
           }) |
           flex;
}

// --- Network ----------------------------------------------------------------
static Element render_network(const AppState& s) {
    return vbox({
               section_box(
                   "Network Status",
                   {
                       label_value("  Network active : ", s.network_active ? "yes" : "no",
                                   s.network_active ? Color::Green : Color::Red),
                       label_value("  Total peers    : ", std::to_string(s.connections)),
                       label_value("  Inbound        : ", std::to_string(s.connections_in)),
                       label_value("  Outbound       : ", std::to_string(s.connections_out)),
                   }),
               section_box(
                   "Node",
                   {
                       label_value("  Client version : ", s.subversion),
                       label_value("  Protocol       : ", std::to_string(s.protocol_version)),
                       label_value("  Relay fee      : ", fmt_satsvb(s.relay_fee)),
                   }),
               filler(),
           }) |
           flex;
}

// --- Peers ------------------------------------------------------------------
static Element render_peers(const AppState& s) {
    if (s.peers.empty()) {
        return vbox({
                   text("No peers connected.") | color(Color::GrayDark) | center,
                   filler(),
               }) |
               flex;
    }

    // Header row
    Elements rows;
    rows.push_back(hbox({
                       text("ID") | bold | size(WIDTH, EQUAL, 5),
                       text("Address") | bold | flex,
                       text("Net") | bold | size(WIDTH, EQUAL, 5),
                       text("I/O") | bold | size(WIDTH, EQUAL, 4),
                       text("Ping ms") | bold | size(WIDTH, EQUAL, 9),
                       text("Recv") | bold | size(WIDTH, EQUAL, 10),
                       text("Sent") | bold | size(WIDTH, EQUAL, 10),
                       text("Height") | bold | size(WIDTH, EQUAL, 9),
                       text("Client") | bold | flex,
                   }) |
                   color(Color::Gold1));
    rows.push_back(separator());

    for (const auto& p : s.peers) {
        std::string ping_str = p.ping_ms >= 0.0 ? [&] {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << p.ping_ms;
            return ss.str();
        }()
                                                : "—";

        Color io_color = p.inbound ? Color::Cyan : Color::Green;

        rows.push_back(hbox({
            text(std::to_string(p.id)) | size(WIDTH, EQUAL, 5),
            text(p.addr) | flex,
            text(p.network.empty() ? "?" : p.network.substr(0, 4)) | size(WIDTH, EQUAL, 5),
            text(p.inbound ? "in" : "out") | color(io_color) | size(WIDTH, EQUAL, 4),
            text(ping_str) | size(WIDTH, EQUAL, 9),
            text(fmt_bytes(p.bytes_recv)) | size(WIDTH, EQUAL, 10),
            text(fmt_bytes(p.bytes_sent)) | size(WIDTH, EQUAL, 10),
            text(fmt_int(p.synced_blocks)) | size(WIDTH, EQUAL, 9),
            text(p.subver) | flex,
        }));
    }

    return vbox({
               vbox(std::move(rows)) | border | flex,
           }) |
           flex;
}

// ============================================================================
// Cookie authentication helpers
// ============================================================================

// Returns the platform-specific default path to Bitcoin Core's .cookie file.
// network is one of: "main", "testnet3", "signet", "regtest".
static std::string cookie_default_path(const std::string& network, const std::string& datadir) {
    std::string base;
    if (!datadir.empty()) {
        base = datadir;
    } else {
        const char* home = std::getenv("HOME");
        if (!home)
            throw std::runtime_error("HOME not set; use --datadir or --cookie to locate .cookie");
#ifdef __APPLE__
        base = std::string(home) + "/Library/Application Support/Bitcoin";
#else
        base = std::string(home) + "/.bitcoin";
#endif
    }
    std::string sub;
    if (network == "testnet3")
        sub = "testnet3/";
    else if (network == "signet")
        sub = "signet/";
    else if (network == "regtest")
        sub = "regtest/";
    return base + "/" + sub + ".cookie";
}

// Reads a Bitcoin Core cookie file and populates cfg.user / cfg.password.
// File format: __cookie__:<password>
static void apply_cookie(RpcConfig& cfg, const std::string& path) {
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
    cfg.user     = line.substr(0, colon);
    cfg.password = line.substr(colon + 1);
}

// ============================================================================
// RPC polling
// ============================================================================
static void poll_rpc(RpcClient& rpc, AppState& state, std::mutex& mtx) {
    try {
        // Blockchain info
        auto bc = rpc.call("getblockchaininfo")["result"];
        // Network info
        auto net = rpc.call("getnetworkinfo")["result"];
        // Mempool info
        auto mp = rpc.call("getmempoolinfo")["result"];
        // Mining info (for hashrate)
        auto mi = rpc.call("getmininginfo")["result"];
        // Peer info
        auto pi = rpc.call("getpeerinfo")["result"];

        std::lock_guard lock(mtx);

        // Blockchain
        state.chain         = bc.value("chain", "—");
        state.blocks        = bc.value("blocks", 0LL);
        state.headers       = bc.value("headers", 0LL);
        state.difficulty    = bc.value("difficulty", 0.0);
        state.progress      = bc.value("verificationprogress", 0.0);
        state.pruned        = bc.value("pruned", false);
        state.ibd           = bc.value("initialblockdownload", false);
        state.bestblockhash = bc.value("bestblockhash", "");

        // Network
        state.connections      = net.value("connections", 0);
        state.connections_in   = net.value("connections_in", 0);
        state.connections_out  = net.value("connections_out", 0);
        state.subversion       = net.value("subversion", "");
        state.protocol_version = net.value("protocolversion", 0);
        state.network_active   = net.value("networkactive", true);
        state.relay_fee        = net.value("relayfee", 0.0);

        // Mempool
        state.mempool_tx      = mp.value("size", 0LL);
        state.mempool_bytes   = mp.value("bytes", 0LL);
        state.mempool_usage   = mp.value("usage", 0LL);
        state.mempool_max     = mp.value("maxmempool", 300000000LL);
        state.mempool_min_fee = mp.value("mempoolminfee", 0.0);
        state.total_fee       = mp.value("total_fee", 0.0);

        // Mining / hashrate
        state.network_hashps = mi.value("networkhashps", 0.0);

        // Peers
        state.peers.clear();
        for (const auto& p : pi) {
            PeerInfo peer;
            peer.id            = p.value("id", 0);
            peer.addr          = p.value("addr", "");
            peer.network       = p.value("network", "");
            peer.subver        = p.value("subver", "");
            peer.inbound       = p.value("inbound", false);
            peer.bytes_sent    = p.value("bytessent", 0LL);
            peer.bytes_recv    = p.value("bytesrecv", 0LL);
            peer.version       = p.value("version", 0);
            peer.synced_blocks = p.value("synced_blocks", 0LL);
            if (p.contains("pingtime") && p["pingtime"].is_number()) {
                peer.ping_ms = p["pingtime"].get<double>() * 1000.0;
            }
            state.peers.push_back(std::move(peer));
        }

        state.connected = true;
        state.error_message.clear();
        state.last_update = now_string();

    } catch (const std::exception& e) {
        std::lock_guard lock(mtx);
        state.connected     = false;
        state.error_message = e.what();
        state.last_update   = now_string();
    }
}

// ============================================================================
// Application entry point (split so main() itself is exception-free)
// ============================================================================
static int run(int argc, char* argv[]) {
    // Parse CLI args
    RpcConfig   cfg;
    int         refresh_secs = 5;
    std::string network      = "main";  // tracks chain for cookie path lookup
    std::string cookie_file;            // explicit --cookie override
    std::string datadir;                // explicit --datadir override
    bool        explicit_creds = false; // true when -u/-P were given

    for (int i = 1; i < argc; ++i) {
        std::string arg  = argv[i];
        auto        next = [&]() -> std::string {
            if (i + 1 < argc)
                return argv[++i];
            return {};
        };
        if (arg == "--host" || arg == "-h")
            cfg.host = next();
        else if (arg == "--port" || arg == "-p")
            cfg.port = std::stoi(next());
        else if (arg == "--user" || arg == "-u") {
            cfg.user       = next();
            explicit_creds = true;
        } else if (arg == "--password" || arg == "-P") {
            cfg.password   = next();
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
                "  q / Escape             Quit\n"
            );
            // clang-format on
            return 0;
        }
    }

    // Apply cookie authentication unless explicit -u/-P credentials were given.
    if (!explicit_creds) {
        std::string path =
            cookie_file.empty() ? cookie_default_path(network, datadir) : cookie_file;
        try {
            apply_cookie(cfg, path);
        } catch (const std::exception& e) {
            // If the user specified --cookie explicitly, fail loudly.
            // Otherwise silently skip — the RPC call will report auth errors.
            if (!cookie_file.empty()) {
                std::fprintf(stderr, "bitcoin-tui: %s\n", e.what());
                return 1;
            }
        }
    }

    // State + RPC client
    AppState   state;
    std::mutex state_mtx;
    RpcClient  rpc(cfg);

    // FTXUI screen
    auto screen = ScreenInteractive::Fullscreen();

    // Tabs
    std::vector<std::string> tab_labels = {"Dashboard", "Mempool", "Network", "Peers"};
    int                      tab_index  = 0;

    auto tab_toggle = Toggle(&tab_labels, &tab_index);

    // Main component: tab toggle only (content rendered manually)
    auto layout = Container::Vertical({tab_toggle});

    auto renderer = Renderer(layout, [&]() -> Element {
        // Snapshot state (brief lock)
        AppState snap;
        {
            std::lock_guard lock(state_mtx);
            snap = state;
        }

        // Tab content
        Element tab_content;
        switch (tab_index) {
        case 0:
            tab_content = render_dashboard(snap);
            break;
        case 1:
            tab_content = render_mempool(snap);
            break;
        case 2:
            tab_content = render_network(snap);
            break;
        case 3:
            tab_content = render_peers(snap);
            break;
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

        Element status_right = hbox({
            snap.refreshing
                ? text(" ↻ refreshing") | color(Color::Yellow)
                : text(" ↻ every " + std::to_string(refresh_secs) + "s") | color(Color::GrayDark),
            text("  [Tab/←/→] switch  [q] quit ") | color(Color::GrayDark),
        });

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

            // Tab bar
            tab_toggle->Render() | border,

            // Content
            tab_content | flex,

            // Status bar
            hbox({status_left, filler(), status_right}) | border,
        });
    });

    // Quit on 'q' or Escape
    auto event_handler = CatchEvent(renderer, [&](const Event& event) -> bool {
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    // Background polling thread
    std::atomic<bool> running{true};
    std::thread       poll_thread([&] {
        // Initial fetch immediately
        {
            std::lock_guard lock(state_mtx);
            state.refreshing = true;
        }
        screen.PostEvent(Event::Custom);

        poll_rpc(rpc, state, state_mtx);

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

            poll_rpc(rpc, state, state_mtx);

            {
                std::lock_guard lock(state_mtx);
                state.refreshing = false;
            }
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(event_handler);

    running = false;
    poll_thread.join();

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
