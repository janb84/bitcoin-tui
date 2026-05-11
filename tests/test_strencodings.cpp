// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util/strencodings.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using util::HexStr;
using util::TryParseHex;
using util::HashHexStr;
using util::TryParseHashHex;

TEST_CASE("HexStr serializes bytes forward in lowercase", "[strencodings]")
{
    const std::vector<uint8_t> bytes = {0x00, 0x0f, 0xa1, 0xff};
    REQUIRE(HexStr(bytes) == "000fa1ff");
    REQUIRE(HexStr(std::vector<uint8_t>{}) == "");
}

TEST_CASE("TryParseHex parses lowercase + uppercase", "[strencodings]")
{
    const auto a = TryParseHex("000fa1ff");
    REQUIRE(a.has_value());
    REQUIRE(*a == std::vector<uint8_t>{0x00, 0x0f, 0xa1, 0xff});

    const auto b = TryParseHex("DEADbeef");
    REQUIRE(b.has_value());
    REQUIRE(*b == std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});

    const auto empty = TryParseHex("");
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());
}

TEST_CASE("TryParseHex rejects malformed input", "[strencodings]")
{
    REQUIRE_FALSE(TryParseHex("abc").has_value());          // odd length
    REQUIRE_FALSE(TryParseHex("zz").has_value());           // non-hex char
    REQUIRE_FALSE(TryParseHex("ab cd").has_value());        // whitespace
    REQUIRE_FALSE(TryParseHex("0x12").has_value());         // 'x' isn't hex
}

TEST_CASE("HexStr / TryParseHex round-trip", "[strencodings]")
{
    const std::vector<uint8_t> in = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    const auto out = TryParseHex(HexStr(in));
    REQUIRE(out.has_value());
    REQUIRE(*out == in);
}

TEST_CASE("HashHexStr serializes bytes high-index-first", "[strencodings]")
{
    // 0x..ff is the high byte of an internally little-endian hash, so
    // it must come first in the rendered string.
    const std::vector<uint8_t> bytes = {0x00, 0x0f, 0xa1, 0xff};
    REQUIRE(HashHexStr(bytes) == "ffa10f00");
    REQUIRE(HashHexStr(std::vector<uint8_t>{}) == "");
}

TEST_CASE("TryParseHashHex reverses parsed bytes", "[strencodings]")
{
    const auto a = TryParseHashHex("ffa10f00");
    REQUIRE(a.has_value());
    REQUIRE(*a == std::vector<uint8_t>{0x00, 0x0f, 0xa1, 0xff});

    REQUIRE_FALSE(TryParseHashHex("zz").has_value());
    REQUIRE_FALSE(TryParseHashHex("abc").has_value());
}

TEST_CASE("HashHexStr / TryParseHashHex round-trip", "[strencodings]")
{
    const std::vector<uint8_t> in(32, 0);
    auto in2 = in;
    in2[0] = 0xab; in2[31] = 0xcd;
    const auto hex = HashHexStr(in2);
    const auto back = TryParseHashHex(hex);
    REQUIRE(back.has_value());
    REQUIRE(*back == in2);
}
