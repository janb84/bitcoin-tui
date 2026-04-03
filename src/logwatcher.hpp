#pragma once

#include <chrono>
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
    TimePoint                time_header;
    bool                     via_compact = false;
    std::optional<TimePoint> time_block;
    int                      txns_requested = 0;
    double                   validation_ms = -1;
    bool                     was_tip = false;       // was this ever the active tip?
    bool                     disconnected = false;   // has this been disconnected?
};

// Consumes LogEvents and maintains a list of recently seen blocks.
// Not thread-safe — caller must synchronize.
class BlockTracker {
  public:
    void process(const LogEvent& ev);

    const std::vector<BlockInfo>& blocks() const { return blocks_; }

  private:
    // Find block by hash, or return nullptr
    BlockInfo* find(const std::string& hash);

    std::vector<BlockInfo> blocks_;
    double pending_connect_ms_ = -1;        // from "Connect block", awaiting UpdateTip
    std::string current_tip_hash_;
    int         current_tip_height_ = 0;
};
