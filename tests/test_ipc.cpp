// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ipc_client.hpp"
#include "mock/ipc/init_v30.h"
#include "util/path_server.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <stdexcept>
#include <string>

using Catch::Matchers::ContainsSubstring;

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
    test_util::PathServer server(init);
    REQUIRE_THROWS_WITH(
        IpcClient(server.path()),
        ContainsSubstring("older than v31.0"));
}
