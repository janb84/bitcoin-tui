#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
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

#include "rpc_client.hpp"

using namespace ftxui;

// ============================================================================
// Block animation parameters
// ============================================================================
static constexpr int BLOCK_ANIM_SLIDE_FRAMES = 12; // frames sliding right (~480 ms)
static constexpr int BLOCK_ANIM_TOTAL_FRAMES = BLOCK_ANIM_SLIDE_FRAMES;

// ============================================================================
// Application state (shared between render thread and RPC polling thread)
// ============================================================================
struct BlockStat {
    int64_t height       = 0;
    int64_t txs          = 0;
    int64_t total_size   = 0;
    int64_t total_weight = 0;
    int64_t time         = 0;
};

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

    // Recent blocks (index 0 = newest, populated by getblockstats)
    std::vector<BlockStat> recent_blocks;
    int64_t                blocks_fetched_at = -1;

    // Block animation
    bool                   block_anim_active = false;
    int                    block_anim_frame  = 0;
    std::vector<BlockStat> block_anim_old; // snapshot before new block arrived

    // Status
    std::string last_update;
    std::string error_message;
    bool        connected  = false;
    bool        refreshing = false;
};

struct TxVin {
    std::string txid;
    int         vout        = 0;
    bool        is_coinbase = false;
};

struct TxVout {
    double      value = 0.0;
    std::string address; // may be empty for non-standard scripts
    std::string type;    // scriptPubKey type
};

struct TxSearchState {
    std::string txid;
    bool        searching = false;
    bool        found     = false;
    bool        is_block  = false; // true = block result, false = tx result
    bool        confirmed = false; // tx only: true = in a block, false = in mempool
    std::string error;
    // Shared (tx)
    int64_t vsize  = 0;
    int64_t weight = 0;
    // Mempool-only
    double  fee         = 0.0; // BTC
    double  fee_rate    = 0.0; // sat/vB
    int64_t ancestors   = 0;
    int64_t descendants = 0;
    int64_t entry_time  = 0;
    // Confirmed tx-only
    std::string blockhash;
    int64_t     block_height  = -1;
    int64_t     confirmations = 0;
    int64_t     blocktime     = 0;
    int         vin_count     = 0;
    int         vout_count    = 0;
    double      total_output  = 0.0; // BTC, sum of all outputs
    // Block result fields
    std::string blk_hash;
    int64_t     blk_height     = 0;
    int64_t     blk_time       = 0;
    int64_t     blk_ntx        = 0;
    int64_t     blk_size       = 0;
    int64_t     blk_weight     = 0;
    double      blk_difficulty = 0.0;
    std::string blk_miner;
    int64_t     blk_confirmations = 0;
    // Input/output navigation
    std::vector<TxVin>  vin_list;
    std::vector<TxVout> vout_list;
    int                 io_selected = -1;
    // Inputs overlay (opened by pressing Enter on the Inputs row)
    bool inputs_overlay_open = false;
    int  input_overlay_sel   = -1;
    // Outputs overlay (opened by pressing Enter on the Outputs row)
    bool outputs_overlay_open = false;
    int  output_overlay_sel   = -1;
};

enum class TxResultKind { Searching, Block, Mempool, Confirmed, Error };

static TxResultKind classify_result(const TxSearchState& ss) {
    if (ss.searching)
        return TxResultKind::Searching;
    if (!ss.found)
        return TxResultKind::Error;
    if (ss.is_block)
        return TxResultKind::Block;
    return ss.confirmed ? TxResultKind::Confirmed : TxResultKind::Mempool;
}

// Navigation index helpers: pure, depend only on TxSearchState contents.
// io_selected: -1=none, 0=block row, 1=inputs(if any), 1or2=outputs(if any)
static int io_inputs_idx(const TxSearchState& ss) { return ss.vin_list.empty() ? -1 : 1; }
static int io_outputs_idx(const TxSearchState& ss) {
    if (ss.vout_list.empty())
        return -1;
    return ss.vin_list.empty() ? 1 : 2;
}
static int io_max_sel(const TxSearchState& ss) {
    int n = 0;
    if (!ss.vin_list.empty())
        n++;
    if (!ss.vout_list.empty())
        n++;
    return n;
}

// Query validators — pure predicates, no captures needed.
static bool is_txid(const std::string& s) {
    if (s.size() != 64)
        return false;
    return std::ranges::all_of(s, [](unsigned char c) { return std::isxdigit(c) != 0; });
}
static bool is_height(const std::string& s) {
    return !s.empty() && s.size() <= 8 &&
           std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c) != 0; });
}

// ============================================================================
// Formatting helpers
// ============================================================================
static std::string fmt_time_ago(int64_t timestamp) {
    auto now = std::chrono::system_clock::now();
    int64_t now_secs = std::chrono::duration_cast<std::chrono::seconds>(
                           now.time_since_epoch()).count();
    int64_t diff = now_secs - timestamp;
    if (diff < 0) return "just now";
    if (diff < 60) return std::to_string(diff) + "s ago";
    if (diff < 3600) return std::to_string(diff / 60) + "m ago";
    if (diff < 86400) return std::to_string(diff / 3600) + "h ago";
    return std::to_string(diff / 86400) + "d ago";
}

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

static std::string fmt_height(int64_t n) {
    std::string s   = std::to_string(n);
    int         pos = static_cast<int>(s.size()) - 3;
    while (pos > 0) {
        s.insert(static_cast<size_t>(pos), "'");
        pos -= 3;
    }
    return s;
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
    if (h >= 1e21)
        ss << h / 1e21 << " ZH/s";
    else if (h >= 1e18)
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
    ss << std::put_time(&tm, "%H:%M:%S");
    return ss.str();
}

static std::string fmt_btc(double btc, int precision = 8) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << btc << " BTC";
    return ss.str();
}

static std::string fmt_age(int64_t secs) {
    if (secs < 60)
        return std::to_string(secs) + "s";
    if (secs < 3600)
        return std::to_string(secs / 60) + "m " + std::to_string(secs % 60) + "s";
    return std::to_string(secs / 3600) + "h " + std::to_string((secs % 3600) / 60) + "m";
}

static std::string trimmed(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

static std::string extract_miner(const std::string& hex) {
    std::string best, run;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int b = std::stoi(hex.substr(i, 2), nullptr, 16);
        if (b >= 0x20 && b < 0x7f && b != '/') {
            run += static_cast<char>(b);
        } else {
            if (run.size() >= 4 && run.size() > best.size())
                best = run;
            run.clear();
        }
    }
    if (run.size() >= 4 && run.size() > best.size())
        best = run;
    if (best.size() > 24)
        best = best.substr(0, 24);
    return best.empty() ? "—" : best;
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
            label_value("  Height      : ", fmt_height(s.blocks)),
            label_value("  Headers     : ", fmt_height(s.headers)),
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
            label_value("  Total fee   : ", fmt_btc(s.total_fee, 4)),
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

    auto stats_section = section_box(
        "Mempool", {
                       label_value("  Transactions    : ", fmt_int(s.mempool_tx)),
                       label_value("  Virtual size    : ", fmt_bytes(s.mempool_bytes)),
                       label_value("  Total fees      : ", fmt_btc(s.total_fee)),
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
                   });

    // Block visualization — vertical fill bars, one column per block.
    Element blocks_section;
    if (s.recent_blocks.empty()) {
        blocks_section =
            section_box("Recent Blocks", {text("  Fetching…") | color(Color::GrayDark)});
    } else {
        const int     BAR_HEIGHT = 6;
        const int     COL_WIDTH  = 10;
        const int64_t MAX_WEIGHT = 4'000'000LL;

        // Determine animation phase.
        bool anim_slide = s.block_anim_active && !s.block_anim_old.empty();

        // During slide: render old blocks minus the last (it slides off the right edge).
        const std::vector<BlockStat>& src        = anim_slide ? s.block_anim_old : s.recent_blocks;
        int                           num        = static_cast<int>(src.size());
        int                           term_width = ftxui::Terminal::Size().dimx;
        int                           max_cols   = std::max(1, (term_width - 4) / (COL_WIDTH + 1));
        int                           max_render = std::min(anim_slide ? std::max(0, num - 1) : num, max_cols);

        // Slide offset grows from 0 → (COL_WIDTH+1) chars over SLIDE_FRAMES frames.
        int left_pad = 0;
        if (anim_slide) {
            double progress = (s.block_anim_frame + 1.0) / BLOCK_ANIM_SLIDE_FRAMES;
            left_pad        = static_cast<int>(std::round(progress * (COL_WIDTH + 1)));
        }

        Elements block_cols;
        for (int i = 0; i < max_render; ++i) {
            const auto& b = src[i];
            double fill   = b.total_weight > 0 ? std::min(1.0, static_cast<double>(b.total_weight) /
                                                                   static_cast<double>(MAX_WEIGHT))
                                               : 0.0;

            Color bar_color = fill > 0.9   ? Color(Color::DarkOrange)
                              : fill > 0.7 ? Color(Color::Yellow)
                                           : Color(Color::Green);

            int filled_rows = static_cast<int>(std::round(fill * BAR_HEIGHT));

            Elements bar;
            for (int r = 0; r < BAR_HEIGHT; ++r) {
                bool is_filled = r >= (BAR_HEIGHT - filled_rows);
                bar.push_back(is_filled ? text("██████████") | color(bar_color)
                                        : text("░░░░░░░░░░") | color(Color::GrayDark));
            }

            if (!block_cols.empty())
                block_cols.push_back(text(" "));

            block_cols.push_back(
                vbox({
                    vbox(std::move(bar)),
                    text(fmt_height(b.height)) | center,
                    text(fmt_int(b.txs) + " tx") | center | color(Color::GrayDark),
                    text(fmt_bytes(b.total_size)) | center | color(Color::GrayDark),
                    text(b.time > 0 ? fmt_time_ago(b.time) : "") | center | color(Color::GrayDark),
                }) |
                size(WIDTH, EQUAL, COL_WIDTH));
        }

        // Compose row: optional slide-offset pad on the left, then blocks.
        Element blocks_row =
            left_pad > 0 ? hbox({text(std::string(left_pad, ' ')), hbox(std::move(block_cols))})
                         : hbox(std::move(block_cols));

        blocks_section =
            section_box("Recent Blocks", {text(""), hbox({text("  "), std::move(blocks_row)})});
    }

    return vbox({
        stats_section,
        blocks_section,
    });
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

    // Right-align text inside a fixed-width cell.
    auto rcell = [](Element e, int w) -> Element {
        return hbox({filler(), std::move(e)}) | size(WIDTH, EQUAL, w);
    };

    // Header row
    Elements rows;
    rows.push_back(hbox({
                       text("ID") | bold | size(WIDTH, EQUAL, 5),
                       text("Address") | bold | flex,
                       text("Net") | bold | size(WIDTH, EQUAL, 5),
                       text("I/O") | bold | size(WIDTH, EQUAL, 4),
                       rcell(text("Ping ms") | bold, 8),
                       rcell(text("Recv") | bold, 10),
                       rcell(text("Sent") | bold, 10),
                       rcell(text("Height") | bold, 9),
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

        Color       io_color = p.inbound ? Color::Cyan : Color::Green;
        std::string net_str  = p.network.empty() ? "?" : p.network.substr(0, 4);

        rows.push_back(hbox({
            text(std::to_string(p.id)) | size(WIDTH, EQUAL, 5),
            text(p.addr) | flex,
            text(net_str) | size(WIDTH, EQUAL, 5),
            text(p.inbound ? "in" : "out") | color(io_color) | size(WIDTH, EQUAL, 4),
            rcell(text(ping_str), 8),
            rcell(text(fmt_bytes(p.bytes_recv)), 10),
            rcell(text(fmt_bytes(p.bytes_sent)), 10),
            rcell(text(fmt_height(p.synced_blocks)), 9),
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
// on_core_ready is called after the 5 fast RPC calls so the UI can render
// core data immediately, before the slower getblockstats fetches complete.
static void poll_rpc(RpcClient& rpc, AppState& state, std::mutex& mtx,
                     const std::function<void()>& on_core_ready = nullptr) {
    // Read cached tip height so we can skip re-fetching block stats when tip hasn't moved.
    int64_t cached_tip = 0;
    {
        std::lock_guard lk(mtx);
        cached_tip = state.blocks_fetched_at;
    }

    try {
        // ── Phase 1: fast calls ──────────────────────────────────────────────
        auto bc  = rpc.call("getblockchaininfo")["result"];
        auto net = rpc.call("getnetworkinfo")["result"];
        auto mp  = rpc.call("getmempoolinfo")["result"];
        auto pi  = rpc.call("getpeerinfo")["result"];

        int64_t new_tip = bc.value("blocks", 0LL);

        // Commit core state immediately so the UI can render before block stats arrive.
        {
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

            // Hashrate derived from difficulty (saves a getmininginfo round-trip):
            // difficulty × 2³² / 600  ≈  expected hashes per second at current difficulty
            state.network_hashps = bc.value("difficulty", 0.0) * 4294967296.0 / 600.0;

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
        }

        // Let the UI render with core data while block stats are fetched.
        if (on_core_ready)
            on_core_ready();

        // ── Phase 2: per-block stats (slow — 7 sequential calls) ────────────
        if (new_tip != cached_tip && new_tip > 0) {
            std::vector<BlockStat> fresh_blocks;
            for (int i = 0; i < 20 && (new_tip - i) >= 0; ++i) {
                try {
                    json      params = {new_tip - i,
                                        json({"height", "txs", "total_size", "total_weight", "time"})};
                    auto      bs     = rpc.call("getblockstats", params)["result"];
                    BlockStat blk;
                    blk.height       = bs.value("height", 0LL);
                    blk.txs          = bs.value("txs", 0LL);
                    blk.total_size   = bs.value("total_size", 0LL);
                    blk.total_weight = bs.value("total_weight", 0LL);
                    blk.time         = bs.value("time", 0LL);
                    fresh_blocks.push_back(blk);
                } catch (...) {
                    break;
                }
            }

            std::lock_guard lock(mtx);
            // Trigger slide animation when a new block arrives.
            if (!state.recent_blocks.empty() && !fresh_blocks.empty()) {
                state.block_anim_old    = state.recent_blocks;
                state.block_anim_frame  = 0;
                state.block_anim_active = true;
            }
            state.recent_blocks     = std::move(fresh_blocks);
            state.blocks_fetched_at = new_tip;
        }

    } catch (const std::exception& e) {
        std::lock_guard lock(mtx);
        state.connected     = false;
        state.error_message = e.what();
        state.last_update   = now_string();
    }
}

// ============================================================================
// Transaction / block lookup — pure: takes config + query, returns result.
// No shared state, no threads, no UI side-effects. Suitable for testing.
// ============================================================================
static TxSearchState perform_tx_search(const RpcConfig& cfg, const std::string& query,
                                       bool is_height, int64_t tip) {
    TxSearchState result;
    result.txid = query;
    try {
        RpcConfig search_cfg       = cfg;
        search_cfg.timeout_seconds = 5;
        RpcClient search_rpc(search_cfg);

        // Helper: populate result with block data from getblock (verbosity 1)
        auto fetch_block = [&](const std::string& hash) {
            auto blk                 = search_rpc.call("getblock", {json(hash), json(1)})["result"];
            result.blk_hash          = blk.value("hash", hash);
            result.blk_height        = blk.value("height", 0LL);
            result.blk_time          = blk.value("time", 0LL);
            result.blk_ntx           = blk.value("nTx", 0LL);
            result.blk_size          = blk.value("size", 0LL);
            result.blk_weight        = blk.value("weight", 0LL);
            result.blk_difficulty    = blk.value("difficulty", 0.0);
            result.blk_confirmations = blk.value("confirmations", 0LL);
            // Extract miner tag from coinbase scriptSig
            if (blk.contains("tx") && blk["tx"].is_array() && !blk["tx"].empty()) {
                std::string coinbase_txid = blk["tx"][0].get<std::string>();
                try {
                    auto coinbase_tx = search_rpc.call("getrawtransaction",
                                                       {json(coinbase_txid), json(true)})["result"];
                    if (coinbase_tx.contains("vin") && coinbase_tx["vin"].is_array() &&
                        !coinbase_tx["vin"].empty()) {
                        std::string cb_hex = coinbase_tx["vin"][0].value("coinbase", "");
                        result.blk_miner   = extract_miner(cb_hex);
                    }
                } catch (...) {
                    result.blk_miner = "—";
                }
            }
            result.is_block = true;
            result.found    = true;
        };

        if (is_height) {
            // Block height search: getblockhash → getblock
            int64_t     height = std::stoll(query);
            auto        hash_r = search_rpc.call("getblockhash", {height})["result"];
            std::string hash   = hash_r.get<std::string>();
            fetch_block(hash);
        } else {
            // 1. Try mempool first
            try {
                auto entry = search_rpc.call("getmempoolentry", {query})["result"];

                if (entry.contains("fees") && entry["fees"].is_object())
                    result.fee = entry["fees"].value("base", 0.0);
                else
                    result.fee = entry.value("fee", 0.0);

                result.vsize       = entry.value("vsize", 0LL);
                result.weight      = entry.value("weight", 0LL);
                result.ancestors   = entry.value("ancestorcount", 0LL);
                result.descendants = entry.value("descendantcount", 0LL);
                result.entry_time  = entry.value("time", 0LL);
                if (result.vsize > 0)
                    result.fee_rate = result.fee * 1e8 / static_cast<double>(result.vsize);
                result.confirmed = false;
                result.found     = true;
            } catch (...) {
                // 2. Try confirmed tx (requires txindex=1)
                try {
                    auto tx =
                        search_rpc.call("getrawtransaction", {json(query), json(true)})["result"];

                    result.vsize         = tx.value("vsize", 0LL);
                    result.weight        = tx.value("weight", 0LL);
                    result.blockhash     = tx.value("blockhash", "");
                    result.confirmations = tx.value("confirmations", 0LL);
                    result.blocktime     = tx.value("blocktime", 0LL);

                    if (tip > 0 && result.confirmations > 0)
                        result.block_height = tip - result.confirmations + 1;

                    if (tx.contains("vin") && tx["vin"].is_array()) {
                        for (const auto& inp : tx["vin"]) {
                            TxVin v;
                            if (inp.contains("coinbase")) {
                                v.is_coinbase = true;
                            } else {
                                v.txid = inp.value("txid", "");
                                v.vout = inp.value("vout", 0);
                            }
                            result.vin_list.push_back(v);
                        }
                        result.vin_count = static_cast<int>(result.vin_list.size());
                    }
                    if (tx.contains("vout") && tx["vout"].is_array()) {
                        for (const auto& out : tx["vout"]) {
                            TxVout v;
                            v.value = out.value("value", 0.0);
                            if (out.contains("scriptPubKey")) {
                                const auto& spk = out["scriptPubKey"];
                                v.type          = spk.value("type", "");
                                if (spk.contains("address"))
                                    v.address = spk.value("address", "");
                            }
                            result.total_output += v.value;
                            result.vout_list.push_back(v);
                        }
                        result.vout_count = static_cast<int>(result.vout_list.size());
                    }
                    result.confirmed = true;
                    result.found     = true;
                } catch (...) {
                    // 3. Fall back: try as block hash
                    fetch_block(query);
                }
            }
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
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

    std::atomic<bool> running{true};

    // FTXUI screen
    auto screen = ScreenInteractive::Fullscreen();

    // Tabs
    std::vector<std::string> tab_labels = {"Dashboard", "Mempool", "Network", "Peers"};
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
