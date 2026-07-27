#include <chrono>
#include <ctime>

#include <catch2/catch_test_macros.hpp>

#include "format.hpp"

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
// to_time_point / fmt_localtime
//
// Local time depends on the machine's timezone, so these assert the shape of
// each format and cross-check the fields against the platform's own conversion
// of the same value.
// ============================================================================

TEST_CASE("to_time_point — seconds since epoch round-trip") {
    auto tp = to_time_point(1'700'000'000.0);
    CHECK(std::chrono::system_clock::to_time_t(tp) == 1'700'000'000);
}

TEST_CASE("fmt_localtime — matches the platform's local time") {
    const std::time_t t  = 1'700'000'000;
    auto              tp = to_time_point(static_cast<double>(t));
    std::tm           tm{};
    // Same split format.hpp uses: localtime_r is POSIX, MSVC has localtime_s
    // with the arguments the other way round.
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char expected[32];
    snprintf(expected, sizeof(expected), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    CHECK(fmt_localtime(tp, TimeFmt::YMDHMS) == expected);

    snprintf(expected, sizeof(expected), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday);
    CHECK(fmt_localtime(tp, TimeFmt::YMD) == expected);

    snprintf(expected, sizeof(expected), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    CHECK(fmt_localtime(tp, TimeFmt::HMS) == expected);
}

TEST_CASE("fmt_localtime — HMSM appends milliseconds") {
    // Build the instant from integral durations. Going through a double would
    // make this platform-dependent: system_clock::duration is microseconds on
    // libc++ but nanoseconds on libstdc++, and scaling 1.7e9 seconds to
    // nanoseconds overruns the mantissa, landing 128ns low so the field reads
    // 249 rather than 250.
    const auto tp = std::chrono::system_clock::time_point{std::chrono::seconds(1'700'000'000)} +
                    std::chrono::milliseconds(250);
    auto out = fmt_localtime(tp, TimeFmt::HMSM);
    REQUIRE(out.size() == 12);
    CHECK(out[8] == '.');
    CHECK(out.substr(9) == "250");
    CHECK(out.substr(0, 8) == fmt_localtime(tp, TimeFmt::HMS));
}

TEST_CASE("to_time_point — fractional seconds stay within a millisecond") {
    // The double conversion is lossy at epoch scale (see above), so pin the
    // accuracy rather than an exact tick.
    const auto exact = std::chrono::system_clock::time_point{std::chrono::seconds(1'700'000'000)} +
                       std::chrono::milliseconds(250);
    const auto err = std::chrono::abs(std::chrono::duration_cast<std::chrono::microseconds>(
        to_time_point(1'700'000'000.25) - exact));
    CHECK(err < std::chrono::milliseconds(1));
}

TEST_CASE("now_string — HH:MM:SS shape") {
    auto s = now_string();
    REQUIRE(s.size() == 8);
    CHECK(s[2] == ':');
    CHECK(s[5] == ':');
}
