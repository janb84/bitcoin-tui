#include <catch2/catch_test_macros.hpp>

#include "state.hpp"

// ============================================================================
// is_txid
// ============================================================================

TEST_CASE("is_txid — valid 64-char hex") {
    CHECK(is_txid("a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"));
    CHECK(is_txid("0000000000000000000000000000000000000000000000000000000000000000"));
    CHECK(is_txid("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
}

TEST_CASE("is_txid — wrong length") {
    CHECK_FALSE(is_txid(""));
    CHECK_FALSE(is_txid("a1b2c3"));
    // 63 chars
    CHECK_FALSE(is_txid("a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b"));
    // 65 chars
    CHECK_FALSE(is_txid("a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c"));
}

TEST_CASE("is_txid — non-hex characters") {
    // 64 chars but contains 'g'
    CHECK_FALSE(is_txid("g1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"));
    // contains space
    CHECK_FALSE(is_txid("a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1 2"));
}

// ============================================================================
// is_height
// ============================================================================

TEST_CASE("is_height — valid heights") {
    CHECK(is_height("0"));
    CHECK(is_height("1"));
    CHECK(is_height("840000"));
    CHECK(is_height("99999999")); // 8 digits
}

TEST_CASE("is_height — empty string") { CHECK_FALSE(is_height("")); }

TEST_CASE("is_height — too long (> 8 digits)") {
    CHECK_FALSE(is_height("123456789")); // 9 digits
}

TEST_CASE("is_height — non-digit characters") {
    CHECK_FALSE(is_height("123abc"));
    CHECK_FALSE(is_height("12 34"));
    CHECK_FALSE(is_height("-1"));
}
