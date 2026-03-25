#include <catch2/catch_test_macros.hpp>

#include "state.hpp"

// ============================================================================
// classify_result
// ============================================================================

TEST_CASE("classify_result — searching") {
    TxSearchState ss;
    ss.searching = true;
    CHECK(classify_result(ss) == TxResultKind::Searching);
}

TEST_CASE("classify_result — not found → Error") {
    TxSearchState ss;
    ss.searching = false;
    ss.found     = false;
    CHECK(classify_result(ss) == TxResultKind::Error);
}

TEST_CASE("classify_result — block result") {
    TxSearchState ss;
    ss.found    = true;
    ss.is_block = true;
    CHECK(classify_result(ss) == TxResultKind::Block);
}

TEST_CASE("classify_result — confirmed tx") {
    TxSearchState ss;
    ss.found     = true;
    ss.is_block  = false;
    ss.confirmed = true;
    CHECK(classify_result(ss) == TxResultKind::Confirmed);
}

TEST_CASE("classify_result — mempool tx") {
    TxSearchState ss;
    ss.found     = true;
    ss.is_block  = false;
    ss.confirmed = false;
    CHECK(classify_result(ss) == TxResultKind::Mempool);
}

// ============================================================================
// io_inputs_idx
// ============================================================================

TEST_CASE("io_inputs_idx — no inputs") {
    TxSearchState ss;
    CHECK(io_inputs_idx(ss) == -1);
}

TEST_CASE("io_inputs_idx — has inputs") {
    TxSearchState ss;
    ss.vin_list.push_back(TxVin{});
    CHECK(io_inputs_idx(ss) == 1);
}

// ============================================================================
// io_outputs_idx
// ============================================================================

TEST_CASE("io_outputs_idx — no outputs") {
    TxSearchState ss;
    CHECK(io_outputs_idx(ss) == -1);
}

TEST_CASE("io_outputs_idx — outputs only (no inputs)") {
    TxSearchState ss;
    ss.vout_list.push_back(TxVout{});
    CHECK(io_outputs_idx(ss) == 1);
}

TEST_CASE("io_outputs_idx — both inputs and outputs") {
    TxSearchState ss;
    ss.vin_list.push_back(TxVin{});
    ss.vout_list.push_back(TxVout{});
    CHECK(io_outputs_idx(ss) == 2);
}

// ============================================================================
// io_max_sel
// ============================================================================

TEST_CASE("io_max_sel — no inputs, no outputs") {
    TxSearchState ss;
    CHECK(io_max_sel(ss) == 0);
}

TEST_CASE("io_max_sel — inputs only") {
    TxSearchState ss;
    ss.vin_list.push_back(TxVin{});
    CHECK(io_max_sel(ss) == 1);
}

TEST_CASE("io_max_sel — outputs only") {
    TxSearchState ss;
    ss.vout_list.push_back(TxVout{});
    CHECK(io_max_sel(ss) == 1);
}

TEST_CASE("io_max_sel — both inputs and outputs") {
    TxSearchState ss;
    ss.vin_list.push_back(TxVin{});
    ss.vout_list.push_back(TxVout{});
    CHECK(io_max_sel(ss) == 2);
}

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

TEST_CASE("is_height — empty string") {
    CHECK_FALSE(is_height(""));
}

TEST_CASE("is_height — too long (> 8 digits)") {
    CHECK_FALSE(is_height("123456789")); // 9 digits
}

TEST_CASE("is_height — non-digit characters") {
    CHECK_FALSE(is_height("123abc"));
    CHECK_FALSE(is_height("12 34"));
    CHECK_FALSE(is_height("-1"));
}
