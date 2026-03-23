#include <catch2/catch_test_macros.hpp>

#include "rpc_client.hpp"

// ============================================================================
// RpcConfig defaults
// ============================================================================

TEST_CASE("RpcConfig defaults") {
    RpcConfig cfg;
    CHECK(cfg.host == "127.0.0.1");
    CHECK(cfg.port == 8332);
    CHECK(cfg.timeout_seconds == 30);
}

// ============================================================================
// RpcAuth is separate from RpcConfig
// ============================================================================

TEST_CASE("RpcAuth default fields are empty") {
    RpcAuth auth;
    CHECK(auth.user.empty());
    CHECK(auth.password.empty());
}

TEST_CASE("RpcAuth can be set independently of RpcConfig") {
    RpcConfig cfg;
    RpcAuth   auth;
    auth.user     = "alice";
    auth.password = "hunter2";

    // config is unchanged
    CHECK(cfg.host == "127.0.0.1");
    CHECK(cfg.port == 8332);

    CHECK(auth.user == "alice");
    CHECK(auth.password == "hunter2");
}

TEST_CASE("RpcConfig copy leaves RpcAuth unchanged") {
    RpcConfig cfg;
    cfg.host = "192.168.1.10";
    cfg.port = 18332;

    RpcConfig cfg2 = cfg;
    CHECK(cfg2.host == "192.168.1.10");
    CHECK(cfg2.port == 18332);
    CHECK(cfg2.timeout_seconds == 30);

    // Changing copy does not affect original
    cfg2.timeout_seconds = 60;
    CHECK(cfg.timeout_seconds == 30);
}
