#include <catch2/catch_test_macros.hpp>

#include "state.hpp"

// ============================================================================
// tools_nav — navigation index helper for the Tools tab
// ============================================================================

TEST_CASE("tools_nav: no results") {
    auto n = tools_nav(false, false);
    CHECK(n.bcast_action == 0);
    CHECK(n.bcast_result == -1);
    CHECK(n.psbt_action  == 1);
    CHECK(n.psbt_result  == -1);
    CHECK(n.max_sel      == 1);
}

TEST_CASE("tools_nav: broadcast result only") {
    auto n = tools_nav(true, false);
    CHECK(n.bcast_action == 0);
    CHECK(n.bcast_result == 1);
    CHECK(n.psbt_action  == 2);
    CHECK(n.psbt_result  == -1);
    CHECK(n.max_sel      == 2);
}

TEST_CASE("tools_nav: psbt result only") {
    auto n = tools_nav(false, true);
    CHECK(n.bcast_action == 0);
    CHECK(n.bcast_result == -1);
    CHECK(n.psbt_action  == 1);
    CHECK(n.psbt_result  == 2);
    CHECK(n.max_sel      == 2);
}

TEST_CASE("tools_nav: both results") {
    auto n = tools_nav(true, true);
    CHECK(n.bcast_action == 0);
    CHECK(n.bcast_result == 1);
    CHECK(n.psbt_action  == 2);
    CHECK(n.psbt_result  == 3);
    CHECK(n.max_sel      == 3);
}

// ============================================================================
// io_inputs_idx / io_outputs_idx / io_max_sel
// ============================================================================

TEST_CASE("io nav: empty tx") {
    TxSearchState ss;
    CHECK(io_inputs_idx(ss)  == -1);
    CHECK(io_outputs_idx(ss) == -1);
    CHECK(io_max_sel(ss)     ==  0);
}

TEST_CASE("io nav: inputs only") {
    TxSearchState ss;
    ss.vin_list.push_back({});
    CHECK(io_inputs_idx(ss)  == 1);
    CHECK(io_outputs_idx(ss) == -1);
    CHECK(io_max_sel(ss)     == 1);
}

TEST_CASE("io nav: outputs only") {
    TxSearchState ss;
    ss.vout_list.push_back({});
    CHECK(io_inputs_idx(ss)  == -1);
    CHECK(io_outputs_idx(ss) == 1);
    CHECK(io_max_sel(ss)     == 1);
}

TEST_CASE("io nav: inputs and outputs") {
    TxSearchState ss;
    ss.vin_list.push_back({});
    ss.vout_list.push_back({});
    CHECK(io_inputs_idx(ss)  == 1);
    CHECK(io_outputs_idx(ss) == 2);
    CHECK(io_max_sel(ss)     == 2);
}

// ============================================================================
// is_txid / is_height
// ============================================================================

TEST_CASE("is_txid: valid") {
    CHECK(is_txid("a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1"));
}

TEST_CASE("is_txid: too short") {
    CHECK(!is_txid("a0b1c2d3"));
}

TEST_CASE("is_txid: non-hex char") {
    CHECK(!is_txid("g0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1"));
}

TEST_CASE("is_height: valid") {
    CHECK(is_height("0"));
    CHECK(is_height("850000"));
    CHECK(is_height("99999999"));
}

TEST_CASE("is_height: too long") {
    CHECK(!is_height("123456789")); // 9 digits
}

TEST_CASE("is_height: empty") {
    CHECK(!is_height(""));
}

TEST_CASE("is_height: non-digit") {
    CHECK(!is_height("123abc"));
}

// ============================================================================
// classify_result
// ============================================================================

TEST_CASE("classify_result: searching") {
    TxSearchState ss;
    ss.searching = true;
    CHECK(classify_result(ss) == TxResultKind::Searching);
}

TEST_CASE("classify_result: not found → error") {
    TxSearchState ss;
    ss.found = false;
    CHECK(classify_result(ss) == TxResultKind::Error);
}

TEST_CASE("classify_result: block") {
    TxSearchState ss;
    ss.found    = true;
    ss.is_block = true;
    CHECK(classify_result(ss) == TxResultKind::Block);
}

TEST_CASE("classify_result: mempool") {
    TxSearchState ss;
    ss.found     = true;
    ss.is_block  = false;
    ss.confirmed = false;
    CHECK(classify_result(ss) == TxResultKind::Mempool);
}

TEST_CASE("classify_result: confirmed") {
    TxSearchState ss;
    ss.found     = true;
    ss.is_block  = false;
    ss.confirmed = true;
    CHECK(classify_result(ss) == TxResultKind::Confirmed);
}
