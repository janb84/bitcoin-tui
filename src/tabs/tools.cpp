#include "tools.hpp"

#include "format.hpp"
#include "render.hpp"

using namespace ftxui;

ToolsTab::ToolsTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                   std::atomic<bool>&                            running,
                   std::function<void(const std::string&, bool)> trigger_search)
    : cfg_(std::move(cfg)), auth_(auth), screen_(screen), running_(running),
      trigger_search_(std::move(trigger_search)) {}

void ToolsTab::open_broadcast_dialog() {
    tools_input_active = true;
    tools_sel          = 0;
    tools_hex_str_.clear();
    screen_.PostEvent(Event::Custom);
}

void ToolsTab::trigger_broadcast(const std::string& hex) {
    if (broadcast_in_flight_.load())
        return;
    broadcast_in_flight_ = true;
    {
        STDLOCK(broadcast_mtx_);
        broadcast_state_ = BroadcastState{.hex = hex, .submitting = true};
    }
    screen_.PostEvent(Event::Custom);
    if (broadcast_thread_.joinable())
        broadcast_thread_.join();
    broadcast_thread_ = std::thread([this, hex] {
        BroadcastState result{.hex = hex};
        try {
            RpcConfig bcast_cfg       = cfg_;
            bcast_cfg.timeout_seconds = 30;
            RpcClient bc(bcast_cfg, auth_);
            json      res      = bc.call("sendrawtransaction", {hex});
            result.result_txid = res["result"].get<std::string>();
            result.success     = true;
        } catch (const std::exception& e) {
            result.result_error = e.what();
            result.success      = false;
        }
        result.has_result    = true;
        broadcast_in_flight_ = false;
        if (!running_.load())
            return;
        {
            STDLOCK(broadcast_mtx_);
            broadcast_state_ = result;
        }
        screen_.PostEvent(Event::Custom);
    });
}

void ToolsTab::do_shutdown() {
    try {
        RpcClient rpc(cfg_, auth_);
        rpc.call("stop", {});
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
    screen_.ExitLoopClosure()();
}

Element ToolsTab::render(const AppState& snap) {
    BroadcastState bs;
    {
        STDLOCK(broadcast_mtx_);
        bs = broadcast_state_;
    }
    return render_tools(snap, bs, tools_input_active, tools_hex_str_, tools_sel);
}

bool ToolsTab::handle_tools_input(const Event& event) {
    if (!tools_input_active)
        return false;
    if (event == Event::Escape) {
        tools_input_active = false;
        tools_hex_str_.clear();
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Return) {
        std::string hex    = trimmed(tools_hex_str_);
        tools_input_active = false;
        tools_hex_str_.clear();
        if (!hex.empty())
            trigger_broadcast(hex);
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Backspace) {
        if (!tools_hex_str_.empty())
            tools_hex_str_.pop_back();
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Tab || event == Event::TabReverse || event == Event::ArrowLeft ||
        event == Event::ArrowRight)
        return true;
    if (event.is_character()) {
        tools_hex_str_ += event.character();
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

bool ToolsTab::handle_keys(const Event& event) {
    bool has_result_row;
    {
        STDLOCK(broadcast_mtx_);
        has_result_row = broadcast_state_.has_result && broadcast_state_.success;
    }
    int shutdown_idx = 1 + (has_result_row ? 1 : 0);
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
                STDLOCK(broadcast_mtx_);
                if (broadcast_state_.has_result && broadcast_state_.success)
                    txid = broadcast_state_.result_txid;
            }
            if (!txid.empty())
                trigger_search_(txid, true);
        }
        return true;
    }
    if (event == Event::ArrowDown || event == Event::ArrowUp) {
        if (event == Event::ArrowDown)
            tools_sel = std::min(tools_sel + 1, shutdown_idx);
        else
            tools_sel = std::max(tools_sel - 1, 0);
        screen_.PostEvent(Event::Custom);
        return true;
    }
    return false;
}

void ToolsTab::join() {
    if (broadcast_thread_.joinable())
        broadcast_thread_.join();
}
