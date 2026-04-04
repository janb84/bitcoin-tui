#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

using Clock     = std::chrono::system_clock;
using TimePoint = Clock::time_point;

// Parsed log events — each variant corresponds to a debug.log line pattern
struct LogEvent {
    TimePoint   timestamp;

    enum Type {
        SAW_HEADER,           // "Saw new [cmpctblock ]header hash=... height=..."
        BLOCK_RECONSTRUCTED,  // "Successfully reconstructed block ... requested"
        BLOCK_RECEIVED,       // "received block ... peer=..."
        CONNECT_START,        // "  - Load block from disk: ...ms" (first bench line in ConnectTip)
        CONNECT_BLOCK,        // "- Connect block: ...ms"
        DISCONNECT_BLOCK,     // "- Disconnect block: ...ms"
        UPDATE_TIP,           // "UpdateTip: new best=... height=..."
    } type;

    // SAW_HEADER
    std::string hash;
    int         height = 0;
    bool        via_compact = false;

    // BLOCK_RECONSTRUCTED
    int         txns_prefilled = 0;
    int         txns_from_mempool = 0;
    int         txns_requested = 0;
    // hash is also set

    // CONNECT_BLOCK / DISCONNECT_BLOCK
    double      elapsed_ms = 0;

    // UPDATE_TIP
    // hash and height are also set
};

// Parse a single debug.log line. Returns nullopt if the line doesn't match
// any pattern we care about.
std::optional<LogEvent> parse_log_line(const std::string& line);

// Parse the ISO 8601 timestamp prefix from a debug.log line.
// Format: "2026-04-03T15:10:45.123456Z" (26 chars + space)
// Returns nullopt if parsing fails.
std::optional<TimePoint> parse_timestamp(const std::string& line);

// A block as assembled from multiple log events
struct BlockInfo {
    int                      height = 0;
    std::string              hash;
    std::string              prev_hash;       // filled by RPC (getblockheader)
    TimePoint                time_header;
    bool                     via_compact = false;
    std::optional<TimePoint> time_block;
    int                      txns_requested = 0;
    double                   validation_ms = -1;
    int                      size_bytes = 0;  // filled by RPC (getblock)
    int                      tx_count = 0;    // filled by RPC (getblock)
    bool                     was_tip = false;       // was this ever the active tip?
    bool                     disconnected = false;   // has this been disconnected?
};

// Consumes LogEvents and maintains a list of recently seen blocks.
// Not thread-safe — caller must synchronize.
class BlockTracker {
  public:
    void process(const LogEvent& ev);

    const std::vector<BlockInfo>& blocks() const { return blocks_; }
    std::vector<BlockInfo>& blocks() { return blocks_; }

    // Returns hashes of blocks that haven't been enriched with RPC data yet,
    // then clears the pending list.
    std::vector<std::string> take_new_hashes();

    // Check if there are pending hashes without consuming them.
    bool has_new_hashes() const { return !new_hashes_.empty(); }

    // Drop blocks more than keep_depth below the current tip height.
    void trim(int keep_depth = 10);

    // Map of hash → prev_hash for tip-tracing
    const std::map<std::string, std::string>& prev_map() const { return prev_map_; }
    void set_prev(const std::string& hash, const std::string& prev) { prev_map_[hash] = prev; }

  private:
    BlockInfo* find(const std::string& hash);

    std::vector<BlockInfo> blocks_;
    std::vector<std::string> new_hashes_;     // blocks needing RPC enrichment
    std::map<std::string, std::string> prev_map_;  // hash → prev_hash
    double pending_connect_ms_ = -1;
    TimePoint pending_connect_ts_{};
    std::string current_tip_hash_;
    int         current_tip_height_ = 0;
};
