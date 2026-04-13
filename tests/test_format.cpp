#include <chrono>
#include <ctime>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "format.hpp"

// ============================================================================
// fmt_int
// ============================================================================

TEST_CASE("fmt_int — no separator below 1000") {
    CHECK(fmt_int(0) == "0");
    CHECK(fmt_int(1) == "1");
    CHECK(fmt_int(999) == "999");
}

TEST_CASE("fmt_int — comma thousands separator") {
    CHECK(fmt_int(1000) == "1,000");
    CHECK(fmt_int(9999) == "9,999");
    CHECK(fmt_int(1234567) == "1,234,567");
    CHECK(fmt_int(1000000000LL) == "1,000,000,000");
}

TEST_CASE("fmt_int — negative numbers") {
    CHECK(fmt_int(-1) == "-1");
    CHECK(fmt_int(-999) == "-999");
    CHECK(fmt_int(-1234) == "-1,234");
    CHECK(fmt_int(-1000000LL) == "-1,000,000");
}

// ============================================================================
// fmt_height
// ============================================================================

TEST_CASE("fmt_height — no separator below 1000") {
    CHECK(fmt_height(0) == "0");
    CHECK(fmt_height(999) == "999");
}

TEST_CASE("fmt_height — apostrophe thousands separator") {
    CHECK(fmt_height(1000) == "1'000");
    CHECK(fmt_height(884231) == "884'231");
    CHECK(fmt_height(1000000) == "1'000'000");
}

// ============================================================================
// fmt_bytes
// ============================================================================

TEST_CASE("fmt_bytes — bytes") {
    CHECK(fmt_bytes(0) == "0 B");
    CHECK(fmt_bytes(1) == "1 B");
    CHECK(fmt_bytes(999) == "999 B");
}

TEST_CASE("fmt_bytes — kilobytes") {
    CHECK(fmt_bytes(1000) == "1.0 KB");
    CHECK(fmt_bytes(1500) == "1.5 KB");
    CHECK(fmt_bytes(10000) == "10.0 KB");
}

TEST_CASE("fmt_bytes — megabytes") {
    CHECK(fmt_bytes(1000000LL) == "1.0 MB");
    CHECK(fmt_bytes(1500000LL) == "1.5 MB");
}

TEST_CASE("fmt_bytes — gigabytes") {
    CHECK(fmt_bytes(1000000000LL) == "1.0 GB");
    CHECK(fmt_bytes(2500000000LL) == "2.5 GB");
}

// ============================================================================
// fmt_difficulty
// ============================================================================

TEST_CASE("fmt_difficulty — small (no suffix)") { CHECK(fmt_difficulty(1234.5) == "1234.50"); }

TEST_CASE("fmt_difficulty — giga") { CHECK(fmt_difficulty(1.5e9) == "1.50 G"); }

TEST_CASE("fmt_difficulty — tera") {
    CHECK(fmt_difficulty(1.0e12) == "1.00 T");
    CHECK(fmt_difficulty(113.76e12) == "113.76 T");
}

TEST_CASE("fmt_difficulty — peta") { CHECK(fmt_difficulty(1.5e15) == "1.50 P"); }

TEST_CASE("fmt_difficulty — exa") { CHECK(fmt_difficulty(2.0e18) == "2.00 E"); }

// ============================================================================
// fmt_hashrate
// ============================================================================

TEST_CASE("fmt_hashrate — H/s") { CHECK(fmt_hashrate(500.0) == "500.00 H/s"); }

TEST_CASE("fmt_hashrate — kH/s") { CHECK(fmt_hashrate(1500.0) == "1.50 kH/s"); }

TEST_CASE("fmt_hashrate — MH/s") { CHECK(fmt_hashrate(5.0e6) == "5.00 MH/s"); }

TEST_CASE("fmt_hashrate — GH/s") { CHECK(fmt_hashrate(3.0e9) == "3.00 GH/s"); }

TEST_CASE("fmt_hashrate — TH/s") { CHECK(fmt_hashrate(2.0e12) == "2.00 TH/s"); }

TEST_CASE("fmt_hashrate — PH/s") { CHECK(fmt_hashrate(1.5e15) == "1.50 PH/s"); }

TEST_CASE("fmt_hashrate — EH/s") { CHECK(fmt_hashrate(700.0e18) == "700.00 EH/s"); }

TEST_CASE("fmt_hashrate — ZH/s") { CHECK(fmt_hashrate(1.0e21) == "1.00 ZH/s"); }

// ============================================================================
// fmt_satsvb
// ============================================================================

TEST_CASE("fmt_satsvb — BTC/kvB to sat/vB") {
    // 0.00001 BTC/kvB == 1 sat/vB
    CHECK(fmt_satsvb(0.00001) == "1.0 sat/vB");
    CHECK(fmt_satsvb(0.00005) == "5.0 sat/vB");
    CHECK(fmt_satsvb(0.00025) == "25.0 sat/vB");
}

// ============================================================================
// fmt_btc
// ============================================================================

TEST_CASE("fmt_btc — default 8 decimal places") {
    CHECK(fmt_btc(0.0) == "0.00000000 BTC");
    CHECK(fmt_btc(1.0) == "1.00000000 BTC");
    CHECK(fmt_btc(0.00001125) == "0.00001125 BTC");
    CHECK(fmt_btc(3.125) == "3.12500000 BTC");
}

TEST_CASE("fmt_btc — custom precision") {
    CHECK(fmt_btc(3.125, 3) == "3.125 BTC");
    CHECK(fmt_btc(0.0, 2) == "0.00 BTC");
}

// ============================================================================
// fmt_age
// ============================================================================

TEST_CASE("fmt_age — seconds (< 60)") {
    CHECK(fmt_age(0) == "0s");
    CHECK(fmt_age(1) == "1s");
    CHECK(fmt_age(59) == "59s");
}

TEST_CASE("fmt_age — minutes and seconds (60–3599)") {
    CHECK(fmt_age(60) == "1m 0s");
    CHECK(fmt_age(90) == "1m 30s");
    CHECK(fmt_age(3599) == "59m 59s");
}

TEST_CASE("fmt_age — hours and minutes (>= 3600)") {
    CHECK(fmt_age(3600) == "1h 0m");
    CHECK(fmt_age(3661) == "1h 1m");
    CHECK(fmt_age(7200) == "2h 0m");
    CHECK(fmt_age(86400) == "24h 0m");
}

// ============================================================================
// fmt_time_ago
// ============================================================================

static int64_t now_secs() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

TEST_CASE("fmt_time_ago — future timestamp") {
    CHECK(fmt_time_ago(now_secs() + 100) == "just now");
}

TEST_CASE("fmt_time_ago — seconds ago") { CHECK(fmt_time_ago(now_secs() - 30) == "30s ago"); }

TEST_CASE("fmt_time_ago — minutes ago") { CHECK(fmt_time_ago(now_secs() - 300) == "5m ago"); }

TEST_CASE("fmt_time_ago — hours ago") { CHECK(fmt_time_ago(now_secs() - 7200) == "2h ago"); }

TEST_CASE("fmt_time_ago — days ago") { CHECK(fmt_time_ago(now_secs() - 86400) == "1d ago"); }

// ============================================================================
// trimmed
// ============================================================================

TEST_CASE("trimmed — no whitespace unchanged") {
    CHECK(trimmed("hello") == "hello");
    CHECK(trimmed("") == "");
}

TEST_CASE("trimmed — leading whitespace") {
    CHECK(trimmed("  hello") == "hello");
    CHECK(trimmed("\thello") == "hello");
    CHECK(trimmed("  \thello") == "hello");
}

TEST_CASE("trimmed — trailing whitespace") {
    CHECK(trimmed("hello  ") == "hello");
    CHECK(trimmed("hello\t") == "hello");
}

TEST_CASE("trimmed — both sides") { CHECK(trimmed("  hello world  ") == "hello world"); }

TEST_CASE("trimmed — only whitespace") {
    CHECK(trimmed("   ") == "");
    CHECK(trimmed("\t\t") == "");
}

TEST_CASE("trimmed — interior spaces preserved") { CHECK(trimmed("  a b c  ") == "a b c"); }

// ============================================================================
// extract_miner
// ============================================================================

TEST_CASE("extract_miner — recognises pool tag") {
    // "AntPool" = 41 6e 74 50 6f 6f 6c
    CHECK(extract_miner("416e74506f6f6c") == "AntPool");
}

TEST_CASE("extract_miner — run shorter than 4 bytes returns em-dash") {
    // "ABC" = 41 42 43  (3 chars)
    CHECK(extract_miner("414243") == "\xe2\x80\x94"); // UTF-8 em-dash
}

TEST_CASE("extract_miner — empty hex returns em-dash") {
    CHECK(extract_miner("") == "\xe2\x80\x94");
}

TEST_CASE("extract_miner — non-printable byte breaks run") {
    // "ABCD" + 0x01 + "EFG" — best run is "ABCD" (4), "EFG" < 4
    CHECK(extract_miner("4142434401454647") == "ABCD");
}

TEST_CASE("extract_miner — forward slash breaks run") {
    // "ABCD" + '/' (2f) + "EFG"
    CHECK(extract_miner("414243442f454647") == "ABCD");
}

TEST_CASE("extract_miner — picks longest run") {
    // "ABC" + 0x01 + "ABCDEFG"
    // first run = "ABC" (3, < 4, ignored); second = "ABCDEFG" (7)
    CHECK(extract_miner("41424301"
                        "41424344454647") == "ABCDEFG");
}

TEST_CASE("extract_miner — truncates at 24 chars") {
    std::string hex;
    for (int i = 0; i < 30; ++i)
        hex += "41"; // 30 × 'A'
    auto result = extract_miner(hex);
    CHECK(result.size() == 24);
    CHECK(result == std::string(24, 'A'));
}
