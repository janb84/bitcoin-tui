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

#include "format.hpp"
#include "poll.hpp"
#include "render.hpp"
#include "rpc_client.hpp"
#include "search.hpp"
#include "state.hpp"

using namespace ftxui;

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
                "  /                      Activate txid search\n"
                "  Enter                  Submit search\n"
                "  Escape                 Cancel input / dismiss result / quit\n"
                "  q                      Quit\n"
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

    // Transaction search state
    TxSearchState              search_state;
    std::vector<TxSearchState> search_history;
    std::mutex                 search_mtx;
    std::string                global_search_str;
    bool                       global_search_active = false;
    std::atomic<bool>          search_in_flight{false};
    std::thread                search_thread;

    // Broadcast tools state
    BroadcastState    broadcast_state;
    std::mutex        broadcast_mtx;
    bool              tools_input_active = false;
    std::string       tools_hex_str;
    std::atomic<bool> broadcast_in_flight{false};
    std::thread       broadcast_thread;
    int               tools_sel = 0; // 0=Broadcast, 1=result txid row (when present)

    std::atomic<bool> running{true};

    // FTXUI screen
    auto screen = ScreenInteractive::Fullscreen();

    // Tabs
    std::vector<std::string> tab_labels = {"Dashboard", "Mempool", "Network", "Peers", "Tools"};
    int                      tab_index  = 0;

    auto tab_toggle = Toggle(&tab_labels, &tab_index);

    // Shared search trigger — switches to Mempool tab when switch_tab is true
    auto trigger_tx_search = [&](const std::string& query, bool switch_tab) {
        if (search_in_flight.load())
            return;
        search_in_flight = true;
        if (switch_tab)
            tab_index = 1;
        {
            std::lock_guard lock(search_mtx);
            if (switch_tab) {
                search_history.clear();
            } else if (!search_state.txid.empty()) {
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

        // Determine whether the query is a block height (all digits) or a hash/txid
        bool query_is_height = !query.empty() && std::ranges::all_of(query, [](unsigned char c) {
            return std::isdigit(c) != 0;
        });

        search_thread = std::thread([&, query, query_is_height, tip_at_search] {
            TxSearchState result = perform_tx_search(cfg, query, query_is_height, tip_at_search);
            search_in_flight     = false;
            if (!running.load())
                return;
            {
                std::lock_guard lock(search_mtx);
                search_state = result;
            }
            screen.PostEvent(Event::Custom);
        });
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
                RpcClient bc(bcast_cfg);
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
        bool overlay_visible;
        bool overlay_is_confirmed_tx;    // confirmed tx (not a block)
        bool overlay_block_row_selected; // block # row highlighted (io_selected == 0)
        bool overlay_inputs_row_sel;     // inputs row highlighted
        bool overlay_outputs_row_sel;    // outputs row highlighted
        bool overlay_inputs_open;        // inputs sub-overlay is open
        bool overlay_outputs_open;       // outputs sub-overlay is open
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
        }

        // Tab content
        Element tab_content;
        switch (tab_index) {
        case 0:
            tab_content = render_dashboard(snap);
            break;
        case 1: {
            TxSearchState ss;
            {
                std::lock_guard lock(search_mtx);
                ss = search_state;
            }

            // Mempool content is always the background layer
            auto base = vbox({render_mempool(snap), filler()}) | flex;

            // No search yet — just show the mempool
            if (ss.txid.empty()) {
                tab_content = std::move(base);
                break;
            }

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
                auto               blk_tm     = *std::localtime(&blk_time_t);
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
        case 2:
            tab_content = render_network(snap);
            break;
        case 3:
            tab_content = render_peers(snap);
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
            : (tab_index == 4)
                ? hbox({text("  [↑/↓] navigate  [↵] activate  [q] quit ") | color(Color::GrayDark)})
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
            : overlay_visible
                ? hbox({text("  [Esc] dismiss  [q] quit ") | color(Color::Yellow)})
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
                        tabs.push_back(i == tab_index ? e | bold | inverted : e | dim);
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

            // Content
            tab_content | flex,

            // Status bar
            hbox({status_left, filler(), status_right}) | border,
        });
    });

    // Event handling: '/' activates global search; Escape/Enter commit or cancel
    auto event_handler = CatchEvent(renderer, [&](const Event& event) -> bool {
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
        // Normal mode
        if (event == Event::Character('/')) {
            global_search_active = true;
            global_search_str.clear();
            screen.PostEvent(Event::Custom);
            return true;
        }
        // Tools tab keys
        if (tab_index == 4) {
            if (event == Event::Character('b')) {
                open_broadcast_dialog();
                return true;
            }
            if (event == Event::Return) {
                if (tools_sel == 0) {
                    open_broadcast_dialog();
                } else if (tools_sel == 1) {
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
                bool has_result_row;
                {
                    std::lock_guard lock(broadcast_mtx);
                    has_result_row = broadcast_state.has_result && broadcast_state.success;
                }
                int max_sel = has_result_row ? 1 : 0; // 0=action; 1=result txid (if any)
                if (event == Event::ArrowDown)
                    tools_sel = std::min(tools_sel + 1, max_sel);
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
    if (broadcast_thread.joinable())
        broadcast_thread.join();
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
