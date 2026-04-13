#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "guarded.hpp"

// ============================================================================
// Basic correctness
// ============================================================================

TEST_CASE("Guarded — default-constructed string is empty") {
    Guarded<std::string> g;
    CHECK(g.get() == "");
}

TEST_CASE("Guarded — value constructor") {
    Guarded<int>         gi(42);
    Guarded<std::string> gs(std::string("hello"));
    CHECK(gi.get() == 42);
    CHECK(gs.get() == "hello");
}

TEST_CASE("Guarded — implicit conversion operator") {
    Guarded<int> g(7);
    int          v = g;
    CHECK(v == 7);
}

TEST_CASE("Guarded — update returns fn result") {
    Guarded<int> g(10);
    int          prev = g.update([](int& v) {
        int old = v;
        v       = 99;
        return old;
    });
    CHECK(prev == 10);
    CHECK(g.get() == 99);
}

TEST_CASE("Guarded — update modifies value") {
    Guarded<std::string> g(std::string("foo"));
    g.update([](std::string& s) { s += "bar"; });
    CHECK(g.get() == "foobar");
}

// ============================================================================
// Concurrent access
// ============================================================================

TEST_CASE("Guarded — concurrent increments produce correct count") {
    constexpr int            kThreads = 8;
    constexpr int            kIter    = 1000;
    Guarded<int>             counter(0);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < kIter; ++j)
                counter.update([](int& v) { ++v; });
        });
    }
    for (auto& t : threads)
        t.join();
    CHECK(counter.get() == kThreads * kIter);
}

TEST_CASE("Guarded — concurrent reads never observe torn writes") {
    // Writer thread continuously stores either 0 or 0xDEADBEEF.
    // Reader threads verify they never see a partial value.
    constexpr int64_t kDeadBeef = 0xDEADBEEF;
    Guarded<int64_t>  g(0);
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        int64_t val = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            val = (val == 0) ? kDeadBeef : 0;
            g.update([val](int64_t& v) { v = val; });
        }
    });

    bool                     torn = false;
    std::vector<std::thread> readers;
    readers.reserve(4);
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            for (int j = 0; j < 5000; ++j) {
                int64_t v = g.get();
                if (v != 0 && v != kDeadBeef)
                    torn = true;
            }
        });
    }
    for (auto& t : readers)
        t.join();
    stop = true;
    writer.join();

    CHECK_FALSE(torn);
}
