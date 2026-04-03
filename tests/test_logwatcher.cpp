#include <catch2/catch_test_macros.hpp>

#include "logwatcher.hpp"

TEST_CASE("parse_timestamp basic", "[logwatcher]") {
    auto tp = parse_timestamp("2026-04-03T15:10:45.123456Z [cmpctblock] foo");
    REQUIRE(tp.has_value());

    auto tt = Clock::to_time_t(*tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    CHECK(tm.tm_year + 1900 == 2026);
    CHECK(tm.tm_mon + 1 == 4);
    CHECK(tm.tm_mday == 3);
    CHECK(tm.tm_hour == 15);
    CHECK(tm.tm_min == 10);
    CHECK(tm.tm_sec == 45);

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(tp->time_since_epoch()) % 1'000'000;
    CHECK(us.count() == 123456);
}

TEST_CASE("parse_timestamp no fractional", "[logwatcher]") {
    auto tp = parse_timestamp("2026-04-03T15:10:45Z body");
    REQUIRE(tp.has_value());
}

TEST_CASE("parse_timestamp milliseconds only", "[logwatcher]") {
    auto tp = parse_timestamp("2026-04-03T15:10:45.123Z body");
    REQUIRE(tp.has_value());
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(tp->time_since_epoch()) % 1'000'000;
    CHECK(us.count() == 123000);
}

TEST_CASE("parse Saw new header", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:10:45.500000Z Saw new header "
        "hash=00000000000000000125cf0e2cd3a8b9 height=420000 peer=5");
    REQUIRE(ev.has_value());
    CHECK(ev->type == LogEvent::SAW_HEADER);
    CHECK(ev->hash == "00000000000000000125cf0e2cd3a8b9");
    CHECK(ev->height == 420000);
    CHECK(ev->via_compact == false);
}

TEST_CASE("parse Saw new cmpctblock header", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:10:45.500000Z [cmpctblock] Saw new cmpctblock header "
        "hash=00000000000000000125cf0e2cd3a8b9 height=420000 peer=5");
    REQUIRE(ev.has_value());
    CHECK(ev->type == LogEvent::SAW_HEADER);
    CHECK(ev->hash == "00000000000000000125cf0e2cd3a8b9");
    CHECK(ev->height == 420000);
    CHECK(ev->via_compact == true);
}

TEST_CASE("parse Successfully reconstructed block", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:10:46.100000Z [cmpctblock] Successfully reconstructed block "
        "00000000000000000125cf0e2cd3a8b9 with 1 txn prefilled, "
        "1842 txn from mempool (incl at least 0 from extra pool) "
        "and 3 txn (1234 bytes) requested");
    REQUIRE(ev.has_value());
    CHECK(ev->type == LogEvent::BLOCK_RECONSTRUCTED);
    CHECK(ev->hash == "00000000000000000125cf0e2cd3a8b9");
    CHECK(ev->txns_prefilled == 1);
    CHECK(ev->txns_from_mempool == 1842);
    CHECK(ev->txns_requested == 3);
}

TEST_CASE("parse received block", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:55:01.200000Z [net] received block "
        "00000000000000000125cf0e2cd3a8b9 peer=7");
    REQUIRE(ev.has_value());
    CHECK(ev->type == LogEvent::BLOCK_RECEIVED);
    CHECK(ev->hash == "00000000000000000125cf0e2cd3a8b9");
}

TEST_CASE("parse Connect block", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:10:50.700000Z [bench] - Connect block: 5230.12ms [1.23s (4.56ms/blk)]");
    REQUIRE(ev.has_value());
    CHECK(ev->type == LogEvent::CONNECT_BLOCK);
    CHECK(ev->elapsed_ms > 5230.0);
    CHECK(ev->elapsed_ms < 5231.0);
}

TEST_CASE("parse Disconnect block", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:55:00.100000Z [bench] - Disconnect block: 12.34ms");
    REQUIRE(ev.has_value());
    CHECK(ev->type == LogEvent::DISCONNECT_BLOCK);
    CHECK(ev->elapsed_ms > 12.3);
    CHECK(ev->elapsed_ms < 12.4);
}

TEST_CASE("parse UpdateTip", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:10:50.800000Z UpdateTip: new best=00000000000000000125cf0e2cd3a8b9 "
        "height=420000 version=0x20000000 log2_work=98.765 tx=850000000 "
        "date='2024-12-15T10:30:45Z' progress=0.995 cache=123.4MiB(4567890)");
    REQUIRE(ev.has_value());
    CHECK(ev->type == LogEvent::UPDATE_TIP);
    CHECK(ev->hash == "00000000000000000125cf0e2cd3a8b9");
    CHECK(ev->height == 420000);
}

TEST_CASE("unrecognized line returns nullopt", "[logwatcher]") {
    auto ev = parse_log_line(
        "2026-04-03T15:10:45.000000Z [net] Some other log message");
    CHECK(!ev.has_value());
}

TEST_CASE("garbage line returns nullopt", "[logwatcher]") {
    auto ev = parse_log_line("not a log line at all");
    CHECK(!ev.has_value());
}

// ============================================================================
// BlockTracker tests
// ============================================================================

// Helper: feed a raw log line into a tracker
static void feed(BlockTracker& t, const char* line) {
    auto ev = parse_log_line(line);
    REQUIRE(ev.has_value());
    t.process(*ev);
}

TEST_CASE("tracker: normal block flow", "[tracker]") {
    BlockTracker t;

    feed(t, "2026-04-03T15:10:45.500000Z Saw new cmpctblock header hash=aaa111 height=100 peer=5");
    REQUIRE(t.blocks().size() == 1);
    CHECK(t.blocks()[0].hash == "aaa111");
    CHECK(t.blocks()[0].height == 100);
    CHECK(t.blocks()[0].via_compact == true);
    CHECK(!t.blocks()[0].time_block.has_value());

    feed(t, "2026-04-03T15:10:45.600000Z [cmpctblock] Successfully reconstructed block aaa111 with 1 txn prefilled, 500 txn from mempool (incl at least 0 from extra pool) and 3 txn (1234 bytes) requested");
    CHECK(t.blocks()[0].time_block.has_value());
    CHECK(t.blocks()[0].txns_requested == 3);

    feed(t, "2026-04-03T15:10:45.700000Z [bench] - Connect block: 18.30ms [0.02s (18.30ms/blk)]");
    feed(t, "2026-04-03T15:10:45.800000Z UpdateTip: new best=aaa111 height=100 version=0x20000000 log2_work=50.0 tx=1000 date='2026-04-03T15:10:00Z' progress=1.0 cache=10.0MiB(100)");

    CHECK(t.blocks()[0].validation_ms > 18.0);
    CHECK(t.blocks()[0].validation_ms < 19.0);
    CHECK(t.blocks()[0].was_tip == true);
}

TEST_CASE("tracker: duplicate header ignored", "[tracker]") {
    BlockTracker t;
    feed(t, "2026-04-03T15:10:45.500000Z Saw new header hash=aaa111 height=100 peer=5");
    feed(t, "2026-04-03T15:10:45.600000Z Saw new header hash=aaa111 height=100 peer=7");
    CHECK(t.blocks().size() == 1);
}

TEST_CASE("tracker: reorg disconnects blocks", "[tracker]") {
    BlockTracker t;

    // Build a chain of two blocks
    feed(t, "2026-04-03T15:00:00.000000Z Saw new header hash=aaa100 height=100 peer=1");
    feed(t, "2026-04-03T15:00:00.100000Z [bench] - Connect block: 20.00ms [0.02s (20.00ms/blk)]");
    feed(t, "2026-04-03T15:00:00.200000Z UpdateTip: new best=aaa100 height=100 version=0x20000000 log2_work=50.0 tx=1000 date='2026-04-03T15:00:00Z' progress=1.0 cache=10.0MiB(100)");

    feed(t, "2026-04-03T15:10:00.000000Z Saw new header hash=bbb101 height=101 peer=1");
    feed(t, "2026-04-03T15:10:00.100000Z [bench] - Connect block: 5000.00ms [5.02s (5000.00ms/blk)]");
    feed(t, "2026-04-03T15:10:00.200000Z UpdateTip: new best=bbb101 height=101 version=0x20000000 log2_work=51.0 tx=2000 date='2026-04-03T15:10:00Z' progress=1.0 cache=10.0MiB(100)");

    CHECK(t.blocks().size() == 2);
    CHECK(t.blocks()[1].was_tip == true);

    // Disconnect block 101
    feed(t, "2026-04-03T15:20:00.000000Z [bench] - Disconnect block: 12.00ms");

    CHECK(t.blocks()[1].disconnected == true);

    // Connect replacement block at height 101 and 102
    feed(t, "2026-04-03T15:20:00.100000Z Saw new header hash=ccc101 height=101 peer=2");
    feed(t, "2026-04-03T15:20:00.200000Z [bench] - Connect block: 15.00ms [0.02s (15.00ms/blk)]");
    feed(t, "2026-04-03T15:20:00.300000Z UpdateTip: new best=ccc101 height=101 version=0x20000000 log2_work=51.0 tx=2000 date='2026-04-03T15:20:00Z' progress=1.0 cache=10.0MiB(100)");

    feed(t, "2026-04-03T15:20:01.000000Z Saw new header hash=ddd102 height=102 peer=2");
    feed(t, "2026-04-03T15:20:01.100000Z [bench] - Connect block: 16.00ms [0.02s (16.00ms/blk)]");
    feed(t, "2026-04-03T15:20:01.200000Z UpdateTip: new best=ddd102 height=102 version=0x20000000 log2_work=52.0 tx=3000 date='2026-04-03T15:20:01Z' progress=1.0 cache=10.0MiB(100)");

    CHECK(t.blocks().size() == 4);

    // bbb101 should be disconnected, ccc101 and ddd102 should be active
    auto* bbb = &t.blocks()[1];
    auto* ccc = &t.blocks()[2];
    auto* ddd = &t.blocks()[3];
    CHECK(bbb->hash == "bbb101");
    CHECK(bbb->disconnected == true);
    CHECK(ccc->hash == "ccc101");
    CHECK(ccc->was_tip == true);
    CHECK(ccc->disconnected == false);
    CHECK(ddd->hash == "ddd102");
    CHECK(ddd->was_tip == true);
}
