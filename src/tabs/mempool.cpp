#include "mempool.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "format.hpp"
#include "render.hpp"
#include "search.hpp"

using namespace ftxui;

MempoolTab::MempoolTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                       std::atomic<bool>& running, AppState& state, std::mutex& state_mtx)
    : cfg_(std::move(cfg)), auth_(auth), screen_(screen), running_(running), state_(state),
      state_mtx_(state_mtx) {}

void MempoolTab::trigger_search(const std::string& query, bool switch_tab, int& tab_index_out) {
    if (search_in_flight_.load())
        return;
    search_in_flight_ = true;
    if (switch_tab)
        tab_index_out = 1;
    {
        StdLockGuard lock(search_mtx_);
        if (switch_tab) {
            search_history_.clear();
        } else if (!search_state_.txid.empty()) {
            search_history_.push_back(search_state_);
        }
        search_state_           = TxSearchState{};
        search_state_.txid      = query;
        search_state_.searching = true;
    }
    screen_.PostEvent(Event::Custom);

    if (search_thread_.joinable())
        search_thread_.join();

    int64_t tip_at_search = 0;
    {
        std::lock_guard lock(state_mtx_);
        tip_at_search = state_.blocks;
    }

    bool query_is_height = !query.empty() && std::ranges::all_of(query, [](unsigned char c) {
        return std::isdigit(c) != 0;
    });

    search_thread_ = std::thread([this, query, query_is_height, tip_at_search] {
        TxSearchState result =
            perform_tx_search(cfg_, auth_, query, query_is_height, tip_at_search);
        search_in_flight_ = false;
        if (!running_.load())
            return;
        {
            StdLockGuard lock(search_mtx_);
            search_state_ = result;
        }
        screen_.PostEvent(Event::Custom);
    });
}

MempoolOverlayInfo MempoolTab::overlay_info() const {
    MempoolOverlayInfo oi;
    StdLockGuard          lock(search_mtx_);
    oi.visible = !search_state_.txid.empty();
    oi.is_confirmed_tx =
        oi.visible && search_state_.found && search_state_.confirmed && !search_state_.is_block;
    int sel            = search_state_.io_selected;
    int inputs_idx     = io_inputs_idx(search_state_);
    int outputs_idx    = io_outputs_idx(search_state_);
    oi.block_row_sel   = oi.is_confirmed_tx && sel == 0;
    oi.inputs_row_sel  = oi.is_confirmed_tx && sel == inputs_idx && inputs_idx >= 0;
    oi.outputs_row_sel = oi.is_confirmed_tx && sel == outputs_idx && outputs_idx >= 0;
    oi.inputs_open     = oi.is_confirmed_tx && search_state_.inputs_overlay_open;
    oi.outputs_open    = oi.is_confirmed_tx && search_state_.outputs_overlay_open;
    return oi;
}

Element MempoolTab::render(const AppState& snap) {
    TxSearchState ss;
    {
        StdLockGuard lock(search_mtx_);
        ss = search_state_;
    }

    auto base = vbox({render_mempool(snap, mempool_sel), filler()}) | flex;

    if (ss.txid.empty())
        return base;

    std::string txid_abbrev =
        ss.txid.size() > 40 ? ss.txid.substr(0, 20) + "\u2026" + ss.txid.substr(ss.txid.size() - 20)
                            : ss.txid;

    Elements result_rows;
    switch (classify_result(ss)) {
    case TxResultKind::Searching:
        result_rows.push_back(text("  Searching\u2026") | color(Color::Yellow));
        break;
    case TxResultKind::Block: {
        auto               blk_time_t = static_cast<std::time_t>(ss.blk_time);
        auto*              blk_tm_ptr = std::localtime(&blk_time_t);
        std::tm            blk_tm     = blk_tm_ptr ? *blk_tm_ptr : std::tm{};
        std::ostringstream blk_time_ss;
        blk_time_ss << std::put_time(&blk_tm, "%Y-%m-%d %H:%M:%S");

        int64_t blk_age =
            ss.blk_time > 0
                ? std::max(int64_t{0}, static_cast<int64_t>(std::time(nullptr)) - ss.blk_time)
                : int64_t{0};

        std::ostringstream diff_ss;
        diff_ss << std::fixed << std::setprecision(2) << ss.blk_difficulty / 1e12 << " T";

        std::string hash_short =
            ss.blk_hash.size() > 48
                ? ss.blk_hash.substr(0, 4) + "\u2026" + ss.blk_hash.substr(ss.blk_hash.size() - 44)
                : ss.blk_hash;

        result_rows.push_back(text("  \u26cf BLOCK") | color(Color::Cyan) | bold);
        result_rows.push_back(label_value("  Height       : ", fmt_height(ss.blk_height)));
        result_rows.push_back(label_value("  Hash         : ", hash_short));
        result_rows.push_back(label_value("  Time         : ", blk_time_ss.str()));
        result_rows.push_back(
            label_value("  Age          : ", ss.blk_time > 0 ? fmt_age(blk_age) : "\u2014"));
        result_rows.push_back(label_value("  Transactions : ", fmt_int(ss.blk_ntx)));
        result_rows.push_back(label_value("  Size         : ", fmt_int(ss.blk_size) + " B"));
        result_rows.push_back(label_value("  Weight       : ", fmt_int(ss.blk_weight) + " WU"));
        result_rows.push_back(label_value("  Difficulty   : ", diff_ss.str()));
        result_rows.push_back(label_value("  Miner        : ", ss.blk_miner));
        result_rows.push_back(label_value("  Confirmations: ", fmt_int(ss.blk_confirmations)));
        break;
    }
    case TxResultKind::Mempool: {
        std::ostringstream rate_ss;
        rate_ss << std::fixed << std::setprecision(1) << ss.fee_rate << " sat/vB";

        int64_t age =
            std::max(int64_t{0}, static_cast<int64_t>(std::time(nullptr)) - ss.entry_time);

        result_rows.push_back(text("  \u25cf MEMPOOL") | color(Color::Yellow) | bold);
        result_rows.push_back(label_value("  Fee         : ", fmt_btc(ss.fee), Color::Green));
        result_rows.push_back(label_value("  Fee rate    : ", rate_ss.str()));
        result_rows.push_back(label_value("  vsize       : ", fmt_int(ss.vsize) + " vB"));
        result_rows.push_back(label_value("  Weight      : ", fmt_int(ss.weight) + " WU"));
        result_rows.push_back(label_value("  Ancestors   : ", fmt_int(ss.ancestors)));
        result_rows.push_back(label_value("  Descendants : ", fmt_int(ss.descendants)));
        result_rows.push_back(label_value("  In mempool  : ", fmt_age(age)));
        break;
    }
    case TxResultKind::Confirmed: {
        int64_t age =
            ss.blocktime > 0
                ? std::max(int64_t{0}, static_cast<int64_t>(std::time(nullptr)) - ss.blocktime)
                : int64_t{0};

        std::string block_num = ss.block_height >= 0 ? fmt_height(ss.block_height) : "\u2014";

        result_rows.push_back(text("  \u2714 CONFIRMED") | color(Color::Green) | bold);
        result_rows.push_back(label_value("  Confirmations: ", fmt_int(ss.confirmations)));
        {
            auto block_row = hbox({text("  Block #      : ") | color(Color::GrayDark),
                                   text(block_num) | color(Color::Cyan) | underlined, filler()});
            if (ss.io_selected == 0)
                block_row = std::move(block_row) | inverted;
            result_rows.push_back(std::move(block_row));
        }
        std::string bh_short =
            ss.blockhash.size() > 48
                ? ss.blockhash.substr(0, 4) + "\u2026" +
                      ss.blockhash.substr(ss.blockhash.size() - 44)
                : ss.blockhash;
        result_rows.push_back(label_value("  Block hash   : ", bh_short));
        result_rows.push_back(
            label_value("  Block age    : ", ss.blocktime > 0 ? fmt_age(age) : "\u2014"));
        result_rows.push_back(label_value("  vsize        : ", fmt_int(ss.vsize) + " vB"));
        result_rows.push_back(label_value("  Weight       : ", fmt_int(ss.weight) + " WU"));
        if (!ss.vin_list.empty()) {
            auto inputs_row =
                hbox({text("  Inputs       : ") | color(Color::GrayDark),
                      text(std::to_string(ss.vin_list.size())) | color(Color::Cyan) | underlined,
                      filler()});
            if (ss.io_selected == 1)
                inputs_row = std::move(inputs_row) | inverted;
            result_rows.push_back(std::move(inputs_row));
        }
        if (!ss.vout_list.empty()) {
            int  outputs_idx = io_outputs_idx(ss);
            auto outputs_row =
                hbox({text("  Outputs      : ") | color(Color::GrayDark),
                      text(std::to_string(ss.vout_list.size())) | color(Color::Cyan) | underlined,
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

    std::string overlay_title =
        classify_result(ss) == TxResultKind::Block ? " Block Search " : " Transaction Search ";
    constexpr int kPanelWidth = 70;
    auto overlay_panel = vbox({hbox({text(overlay_title) | bold | color(Color::Gold1), filler(),
                                     text(" " + txid_abbrev + " ") | color(Color::GrayDark)}),
                               separator(), vbox(std::move(result_rows))}) |
                         border | size(WIDTH, EQUAL, kPanelWidth);

    constexpr int kIOPanelWidth = 84;

    auto build_io_panel = [&](std::string title, Elements rows, int n, int win,
                              int top) -> Element {
        if (n > win) {
            rows.push_back(
                hbox({filler(), text(std::to_string(top + 1) + "\u2013" +
                                     std::to_string(top + win) + " / " + std::to_string(n)) |
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
                std::string addr = v.address.size() > 60
                                       ? v.address.substr(0, 28) + "\u2026" +
                                             v.address.substr(v.address.size() - 28)
                                       : v.address;
                label += "  " + addr;
            } else if (!v.type.empty()) {
                label += "  [" + v.type + "]";
            }
            bool selected = (i == sel);
            auto row      = hbox(
                {text("  [" + std::to_string(i) + "] ") | color(Color::GrayDark), text(label)});
            if (selected)
                row = std::move(row) | inverted;
            rows.push_back(std::move(row));
        }
        return vbox({filler(),
                     hbox({filler(),
                           build_io_panel(" Outputs (" + std::to_string(n) + ") ", std::move(rows),
                                          n, win, top),
                           filler()}),
                     filler()}) |
               flex;
    }

    if (ss.inputs_overlay_open && !ss.vin_list.empty()) {
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
            const auto& v     = ss.vin_list[i];
            std::string label = v.is_coinbase ? "coinbase" : v.txid + ":" + std::to_string(v.vout);
            bool        selected = (i == sel);
            auto        row      = hbox(
                {text("  [" + std::to_string(i) + "] ") | color(Color::GrayDark),
                             text(label) | (v.is_coinbase ? color(Color::GrayDark) : color(Color::Default))});
            if (selected)
                row = std::move(row) | inverted;
            rows.push_back(std::move(row));
        }
        return vbox({filler(),
                     hbox({filler(),
                           build_io_panel(" Inputs (" + std::to_string(n) + ") ", std::move(rows),
                                          n, win, top),
                           filler()}),
                     filler()}) |
               flex;
    }

    return vbox({filler(), hbox({filler(), std::move(overlay_panel), filler()}), filler()}) | flex;
}

std::optional<bool> MempoolTab::handle_tx_overlay(const Event& event) {
    // Outputs sub-overlay
    {
        bool outputs_open = false;
        {
            StdLockGuard lock(search_mtx_);
            outputs_open = search_state_.found && search_state_.confirmed &&
                           !search_state_.is_block && search_state_.outputs_overlay_open;
        }
        if (outputs_open) {
            if (event == Event::Escape) {
                {
                    StdLockGuard lock(search_mtx_);
                    search_state_.outputs_overlay_open = false;
                }
                screen_.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::ArrowDown || event == Event::ArrowUp) {
                {
                    StdLockGuard lock(search_mtx_);
                    int             n = static_cast<int>(search_state_.vout_list.size());
                    if (event == Event::ArrowDown)
                        search_state_.output_overlay_sel =
                            std::min(search_state_.output_overlay_sel + 1, n - 1);
                    else
                        search_state_.output_overlay_sel =
                            std::max(search_state_.output_overlay_sel - 1, -1);
                }
                screen_.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Character('q')) {
                screen_.ExitLoopClosure()();
                return true;
            }
            return false;
        }
    }
    // Inputs sub-overlay
    {
        bool inputs_open = false;
        {
            StdLockGuard lock(search_mtx_);
            inputs_open = search_state_.found && search_state_.confirmed &&
                          !search_state_.is_block && search_state_.inputs_overlay_open;
        }
        if (inputs_open) {
            if (event == Event::Escape) {
                {
                    StdLockGuard lock(search_mtx_);
                    search_state_.inputs_overlay_open = false;
                }
                screen_.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::ArrowDown || event == Event::ArrowUp) {
                {
                    StdLockGuard lock(search_mtx_);
                    int             n = static_cast<int>(search_state_.vin_list.size());
                    if (event == Event::ArrowDown)
                        search_state_.input_overlay_sel =
                            std::min(search_state_.input_overlay_sel + 1, n - 1);
                    else
                        search_state_.input_overlay_sel =
                            std::max(search_state_.input_overlay_sel - 1, -1);
                }
                screen_.PostEvent(Event::Custom);
                return true;
            }
            if (event == Event::Return) {
                std::string query;
                {
                    StdLockGuard lock(search_mtx_);
                    int             sel = search_state_.input_overlay_sel;
                    if (sel >= 0 && sel < static_cast<int>(search_state_.vin_list.size()) &&
                        !search_state_.vin_list[sel].is_coinbase)
                        query = search_state_.vin_list[sel].txid;
                }
                if (!query.empty()) {
                    int dummy = 0;
                    trigger_search(query, false, dummy);
                    return true;
                }
            }
            if (event == Event::Character('q')) {
                screen_.ExitLoopClosure()();
                return true;
            }
            return false;
        }
    }
    return std::nullopt;
}

bool MempoolTab::handle_navigation(const Event& event) {
    bool has_overlay;
    {
        StdLockGuard lock(search_mtx_);
        has_overlay = !search_state_.txid.empty();
    }
    if (has_overlay)
        return false;

    if (event == Event::ArrowDown && mempool_sel < 0) {
        int n;
        {
            std::lock_guard lock(state_mtx_);
            n = static_cast<int>(state_.recent_blocks.size());
        }
        if (n > 0) {
            mempool_sel = 0;
            screen_.PostEvent(Event::Custom);
            return true;
        }
    }
    if (mempool_sel >= 0 && (event == Event::ArrowLeft || event == Event::ArrowRight)) {
        int n;
        {
            std::lock_guard lock(state_mtx_);
            n = static_cast<int>(state_.recent_blocks.size());
        }
        if (n <= 0) {
            mempool_sel = -1;
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::ArrowLeft)
            mempool_sel = std::max(mempool_sel - 1, 0);
        else
            mempool_sel = std::min(mempool_sel + 1, n - 1);
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Return && mempool_sel >= 0) {
        std::string height_str;
        {
            std::lock_guard lock(state_mtx_);
            if (mempool_sel < static_cast<int>(state_.recent_blocks.size()))
                height_str = std::to_string(state_.recent_blocks[mempool_sel].height);
        }
        if (!height_str.empty()) {
            int dummy = 0;
            trigger_search(height_str, false, dummy);
            return true;
        }
    }
    if (event == Event::Escape && mempool_sel >= 0) {
        mempool_sel = -1;
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

bool MempoolTab::handle_io_nav(const Event& event) {
    if (event != Event::ArrowDown && event != Event::ArrowUp)
        return false;
    bool handled = false;
    {
        StdLockGuard lock(search_mtx_);
        if (search_state_.found && search_state_.confirmed && !search_state_.is_block) {
            int max_sel = io_max_sel(search_state_);
            if (event == Event::ArrowDown)
                search_state_.io_selected = std::min(search_state_.io_selected + 1, max_sel);
            else
                search_state_.io_selected = std::max(search_state_.io_selected - 1, -1);
            handled = true;
        }
    }
    if (handled) {
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

bool MempoolTab::handle_enter(const Event& event) {
    if (event != Event::Return)
        return false;
    bool        open_inputs = false, open_outputs = false;
    std::string query;
    {
        StdLockGuard lock(search_mtx_);
        if (search_state_.found && search_state_.confirmed && !search_state_.is_block) {
            int sel         = search_state_.io_selected;
            int inputs_idx  = io_inputs_idx(search_state_);
            int outputs_idx = io_outputs_idx(search_state_);
            if (sel == inputs_idx && inputs_idx >= 0) {
                search_state_.inputs_overlay_open = true;
                search_state_.input_overlay_sel   = -1;
                open_inputs                       = true;
            } else if (sel == outputs_idx && outputs_idx >= 0) {
                search_state_.outputs_overlay_open = true;
                search_state_.output_overlay_sel   = -1;
                open_outputs                       = true;
            } else if (sel == 0) {
                query = search_state_.blockhash;
            }
        }
    }
    if (open_inputs || open_outputs) {
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (!query.empty()) {
        int dummy = 0;
        trigger_search(query, false, dummy);
        return true;
    }
    return false;
}

bool MempoolTab::handle_escape(const Event& event) {
    if (event != Event::Escape)
        return false;
    bool had_overlay = false;
    {
        StdLockGuard lock(search_mtx_);
        if (!search_history_.empty()) {
            search_state_ = search_history_.back();
            search_history_.pop_back();
            had_overlay = true;
        } else if (!search_state_.txid.empty()) {
            search_state_ = TxSearchState{};
            had_overlay   = true;
        }
    }
    if (had_overlay) {
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

void MempoolTab::join() {
    if (search_thread_.joinable())
        search_thread_.join();
}
