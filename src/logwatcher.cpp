#include "logwatcher.hpp"

#include <set>

#include <re2/re2.h>

namespace {

// Compiled once, reused for every line
const re2::RE2 re_timestamp(
    R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,6}))?Z )"
);

const re2::RE2 re_saw_header(
    R"(Saw new (cmpctblock )?header hash=(\w+) height=(\d+))"
);

const re2::RE2 re_reconstructed(
    R"(Successfully reconstructed block (\w+) with (\d+) txn prefilled, (\d+) txn from mempool \(incl at least \d+ from extra pool\) and (\d+) txn)"
);

const re2::RE2 re_received_block(
    R"(received block (\w+) peer=)"
);

const re2::RE2 re_connect_block(
    R"(- Connect block: ([0-9.]+)ms)"
);

const re2::RE2 re_disconnect_block(
    R"(- Disconnect block: ([0-9.]+)ms)"
);

const re2::RE2 re_update_tip(
    R"(UpdateTip: new best=(\w+) height=(\d+))"
);

int to_micros(const std::string& frac) {
    int val = std::stoi(frac);
    for (size_t i = frac.size(); i < 6; ++i) val *= 10;
    return val;
}

} // namespace

std::optional<TimePoint> parse_timestamp(const std::string& line) {
    std::string y, mo, d, h, mi, s, frac;
    if (!re2::RE2::PartialMatch(line, re_timestamp, &y, &mo, &d, &h, &mi, &s, &frac))
        return std::nullopt;

    std::tm tm{};
    tm.tm_year = std::stoi(y) - 1900;
    tm.tm_mon  = std::stoi(mo) - 1;
    tm.tm_mday = std::stoi(d);
    tm.tm_hour = std::stoi(h);
    tm.tm_min  = std::stoi(mi);
    tm.tm_sec  = std::stoi(s);

    int microseconds = frac.empty() ? 0 : to_micros(frac);
    auto tp = Clock::from_time_t(timegm(&tm));
    return tp + std::chrono::microseconds(microseconds);
}

std::optional<LogEvent> parse_log_line(const std::string& line) {
    LogEvent ev;

    // Parse timestamp
    std::string y, mo, d, h, mi, s, frac;
    if (!re2::RE2::PartialMatch(line, re_timestamp, &y, &mo, &d, &h, &mi, &s, &frac))
        return std::nullopt;

    std::tm tm{};
    tm.tm_year = std::stoi(y) - 1900;
    tm.tm_mon  = std::stoi(mo) - 1;
    tm.tm_mday = std::stoi(d);
    tm.tm_hour = std::stoi(h);
    tm.tm_min  = std::stoi(mi);
    tm.tm_sec  = std::stoi(s);
    int microseconds = frac.empty() ? 0 : to_micros(frac);
    ev.timestamp = Clock::from_time_t(timegm(&tm)) + std::chrono::microseconds(microseconds);

    // Match against event patterns
    std::string s1, s2, s3, s4;

    if (re2::RE2::PartialMatch(line, re_saw_header, &s1, &s2, &s3)) {
        ev.type = LogEvent::SAW_HEADER;
        ev.via_compact = !s1.empty();
        ev.hash = s2;
        ev.height = std::stoi(s3);
        return ev;
    }

    if (re2::RE2::PartialMatch(line, re_reconstructed, &s1, &s2, &s3, &s4)) {
        ev.type = LogEvent::BLOCK_RECONSTRUCTED;
        ev.hash = s1;
        ev.txns_prefilled = std::stoi(s2);
        ev.txns_from_mempool = std::stoi(s3);
        ev.txns_requested = std::stoi(s4);
        return ev;
    }

    if (re2::RE2::PartialMatch(line, re_received_block, &s1)) {
        ev.type = LogEvent::BLOCK_RECEIVED;
        ev.hash = s1;
        return ev;
    }

    if (re2::RE2::PartialMatch(line, re_connect_block, &s1)) {
        ev.type = LogEvent::CONNECT_BLOCK;
        ev.elapsed_ms = std::stod(s1);
        return ev;
    }

    if (re2::RE2::PartialMatch(line, re_disconnect_block, &s1)) {
        ev.type = LogEvent::DISCONNECT_BLOCK;
        ev.elapsed_ms = std::stod(s1);
        return ev;
    }

    if (re2::RE2::PartialMatch(line, re_update_tip, &s1, &s2)) {
        ev.type = LogEvent::UPDATE_TIP;
        ev.hash = s1;
        ev.height = std::stoi(s2);
        return ev;
    }

    return std::nullopt;
}

// ============================================================================
// BlockTracker
// ============================================================================

void BlockTracker::trim(int keep_depth) {
    if (current_tip_height_ == 0) return;
    int min_height = current_tip_height_ - keep_depth;
    // Build set of surviving hashes
    std::set<std::string> surviving;
    std::erase_if(blocks_, [&](const BlockInfo& b) {
        if (b.height < min_height) return true;
        surviving.insert(b.hash);
        return false;
    });
    // Remove stale entries from new_hashes_
    std::erase_if(new_hashes_, [&](const std::string& h) {
        return surviving.find(h) == surviving.end();
    });
}

std::vector<std::string> BlockTracker::take_new_hashes() {
    std::vector<std::string> result;
    result.swap(new_hashes_);
    return result;
}

BlockInfo* BlockTracker::find(const std::string& hash) {
    for (auto& b : blocks_) {
        if (b.hash == hash) return &b;
    }
    return nullptr;
}

void BlockTracker::process(const LogEvent& ev) {
    switch (ev.type) {

    case LogEvent::SAW_HEADER: {
        if (!find(ev.hash)) {
            BlockInfo bi;
            bi.height = ev.height;
            bi.hash = ev.hash;
            bi.time_header = ev.timestamp;
            bi.via_compact = ev.via_compact;
            blocks_.push_back(std::move(bi));
            new_hashes_.push_back(ev.hash);
        }
        break;
    }

    case LogEvent::BLOCK_RECONSTRUCTED: {
        auto* bi = find(ev.hash);
        if (bi) {
            bi->time_block = ev.timestamp;
            bi->via_compact = true;
            bi->txns_requested = ev.txns_requested;
        }
        break;
    }

    case LogEvent::BLOCK_RECEIVED: {
        auto* bi = find(ev.hash);
        if (bi) {
            bi->time_block = ev.timestamp;
        }
        break;
    }

    case LogEvent::CONNECT_BLOCK: {
        pending_connect_ms_ = ev.elapsed_ms;
        pending_connect_ts_ = ev.timestamp;
        break;
    }

    case LogEvent::DISCONNECT_BLOCK: {
        auto* bi = find(current_tip_hash_);
        if (bi) {
            bi->disconnected = true;
        }
        --current_tip_height_;
        current_tip_hash_.clear();
        for (auto& b : blocks_) {
            if (b.height == current_tip_height_ && b.was_tip && !b.disconnected) {
                current_tip_hash_ = b.hash;
                break;
            }
        }
        break;
    }

    case LogEvent::UPDATE_TIP: {
        auto* bi = find(ev.hash);
        if (bi) {
            bi->was_tip = true;
            bi->disconnected = false;
            if (pending_connect_ms_ >= 0) {
                bi->validation_ms = pending_connect_ms_;
            }
            // Fill in time_block from the earliest available source:
            // BLOCK_RECEIVED/BLOCK_RECONSTRUCTED (already set), or
            // CONNECT_BLOCK timestamp, or UpdateTip timestamp
            if (!bi->time_block) {
                if (pending_connect_ts_ != TimePoint{}) {
                    bi->time_block = pending_connect_ts_;
                } else {
                    bi->time_block = ev.timestamp;
                }
            }
        }
        pending_connect_ms_ = -1;
        pending_connect_ts_ = {};
        current_tip_hash_ = ev.hash;
        current_tip_height_ = ev.height;
        trim();
        break;
    }

    } // switch
}
