// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for IpcClient. Uses an in-process Cap'n Proto server (see
// tests/util/path_server.h) hosting mock interfaces::Init / Mining
// implementations from tests/mock/ipc/. No live bitcoin-node required.
//
// The mock setup mirrors the pattern used in Bitcoin Core's
// src/ipc/test/ipc_test.cpp (IpcSocketPairTest) and the sv2-tp project's
// src/test/sv2_tp_tester.cpp (TPTester).

#include "ipc_client.hpp"

#include "interfaces/types.h"
#include "mock/ipc/init.h"
#include "mock/ipc/init_v30.h"
#include "mock/ipc/mining_v31.h"
#include "util/path_server.h"
#include "util/strencodings.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;
using mock_ipc::MiningV31;
using test_util::PathServer;

namespace {
// Wrap a v31 Mining mock in an Init mock the PathServer can serve.
std::shared_ptr<interfaces::Init> MakeV31Init(std::shared_ptr<interfaces::Mining> mining)
{
    return std::make_shared<mock_ipc::Init>(std::move(mining));
}

// Construct an interfaces::uint256 from a big-endian (RPC-style) hex
// string. Test-only convenience: the production layer parses hashes
// directly via util::TryParseHashHex.
interfaces::uint256 HashFromHex(std::string_view hex)
{
    const auto bytes = util::TryParseHashHex(hex);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == interfaces::uint256::WIDTH);
    return interfaces::uint256(bytes->data(), bytes->data() + bytes->size());
}
} // namespace

TEST_CASE("IpcClient throws on a bogus socket path", "[ipc]")
{
    REQUIRE_THROWS_AS(
        IpcClient("/this/path/should/never/exist.sock"),
        std::runtime_error);
}

TEST_CASE("IpcClient rejects oversized socket paths up front", "[ipc]")
{
    // Linux's sun_path is 108 bytes, macOS's is 104. 200 is comfortably
    // longer than both, so this must throw on every supported platform.
    const std::string huge(200, 'x');
    REQUIRE_THROWS_WITH(IpcClient(huge), ContainsSubstring("path too long"));
}

TEST_CASE("IpcClient rejects a v30.x server up front", "[ipc]")
{
    // mock_ipc::InitV30 simulates v30's wire-level "Method not implemented;
    // methodId = 3" response when bitcoin-tui calls makeMining. The
    // IpcClient constructor must turn that into a friendly runtime_error
    // mentioning "older than v31.0".
    auto init = std::make_shared<mock_ipc::InitV30>();
    PathServer server(init);
    REQUIRE_THROWS_WITH(
        IpcClient(server.path()),
        ContainsSubstring("older than v31.0"));
}

TEST_CASE("IpcClient connects to a v31-style mock and reads the tip", "[ipc]")
{
    auto mining = std::make_shared<MiningV31>(HashFromHex(
        "00000000000000000001020304050607080900000000000000000000000000ab"));
    PathServer server(MakeV31Init(mining));

    IpcClient client(server.path());
    auto tip = client.wait_tip_changed("", 100ms);
    REQUIRE(tip.has_value());
    REQUIRE(*tip == "00000000000000000001020304050607080900000000000000000000000000ab");
}

TEST_CASE("IpcClient surfaces null hash (interrupt / shutdown) as nullopt", "[ipc]")
{
    // No tip set -> the mock returns null BlockRef immediately when the
    // wait deadline fires (no interrupt and an empty tip both produce a
    // null hash on the wire).
    auto mining = std::make_shared<MiningV31>();
    PathServer server(MakeV31Init(mining));

    IpcClient client(server.path());
    auto tip = client.wait_tip_changed("", 50ms);
    REQUIRE(!tip.has_value());
}

TEST_CASE("IpcClient.interrupt() unblocks an in-flight wait_tip_changed", "[ipc]")
{
    auto mining = std::make_shared<MiningV31>();
    PathServer server(MakeV31Init(mining));

    IpcClient client(server.path());
    std::atomic<bool> done{false};
    std::optional<std::string> result;
    // Use a long server-side timeout so that if interrupt() is a no-op the
    // waiter only completes when the mock's wait_until deadline fires —
    // which the wall-clock bound below will catch as a regression.
    std::thread waiter([&] {
        result = client.wait_tip_changed("", 30s);
        done = true;
    });
    std::this_thread::sleep_for(50ms);
    const auto t0 = std::chrono::steady_clock::now();
    client.interrupt();
    waiter.join();
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE(done.load());
    REQUIRE(!result.has_value());
    // interrupt() must propagate to the server within a tight bound; if
    // it's a no-op the waiter would block until the 30s server deadline.
    REQUIRE(elapsed < 2s);
}

TEST_CASE("IpcClient passes current_tip_hex through to the server", "[ipc]")
{
    auto mining = std::make_shared<MiningV31>(HashFromHex(
        "00000000000000000001020304050607080900000000000000000000000000ab"));
    PathServer server(MakeV31Init(mining));

    IpcClient client(server.path());
    const std::string sent =
        "deadbeefcafebabe"
        "0102030405060708"
        "090a0b0c0d0e0f10"
        "1112131415161718";
    auto tip = client.wait_tip_changed(sent, 100ms);
    REQUIRE(tip.has_value());
    // Server must observe the exact hash the client passed in, not an
    // empty/zero hash. This catches a regression where wait_tip_changed
    // drops the current_tip argument before sending.
    const auto observed = mining->last_current_tip();
    REQUIRE(util::HashHexStr(std::span<const uint8_t>{observed}) == sent);
}
