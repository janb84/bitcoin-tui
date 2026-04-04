#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "guarded.hpp"
#include "logwatcher.hpp"
#include "tabs/tab.hpp"

struct BlockEvent {
    int                                                  height = 0;
    std::string                                          hash;
    std::chrono::system_clock::time_point                time_header;
    bool                                                 via_compact = false;
    std::optional<std::chrono::system_clock::time_point> time_block;
    int                                                  txns_requested = 0;
    double                                               validation_ms  = -1;
    std::string                                          tips;
    int                                                  size_bytes = 0;
    int                                                  tx_count   = 0;
};

struct ChainTipInfo {
    char        label  = ' ';
    int         height = 0;
    std::string hash;
    std::string status;
};

struct SlowBlocksState {
    std::vector<BlockEvent>   blocks;
    std::vector<ChainTipInfo> tips;
    int64_t                   lines_parsed = 0;
    std::string               status;           // "reading...", "tailing", "error: ..."
    std::deque<std::string>   recent_log_lines; // last N matched log lines
    std::optional<std::chrono::system_clock::time_point>
        validating_since;  // set during slow validation
    std::string warning;   // e.g. missing log categories
};

class SlowBlocksTab : public Tab {
  public:
    SlowBlocksTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ftxui::ScreenInteractive& screen,
                  std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs,
                  std::string debug_log_path);
    ~SlowBlocksTab() override = default;

    ftxui::Element render(const AppState& snap) override;
    ftxui::Element key_hints(const AppState& snap) const override;
    void           join() override;

  private:
    std::string              debug_log_path_;
    Guarded<SlowBlocksState> sb_state_;
    Guarded<BlockTracker>    tracker_;
    std::thread              log_thread_;
    std::thread              rpc_thread_;
    std::thread              tick_thread_;
};
