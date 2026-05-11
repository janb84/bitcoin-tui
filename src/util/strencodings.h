// Copyright (c) 2009-present The Bitcoin Core developers
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_UTIL_STRENCODINGS_H
#define BITCOIN_TUI_UTIL_STRENCODINGS_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace util {

//! Convert a byte buffer to a forward (low-index-first) lowercase hex
//! string. Mirrors Bitcoin Core's util/strencodings.h::HexStr.
inline std::string HexStr(std::span<const uint8_t> bytes)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(hex[(b >> 4) & 0xF]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

//! Parse an even-length lowercase/uppercase hex string into bytes. Returns
//! std::nullopt on any non-hex character or odd length. Mirrors Bitcoin
//! Core's util/strencodings.h::TryParseHex.
inline std::optional<std::vector<uint8_t>> TryParseHex(std::string_view hex)
{
    if (hex.size() % 2 != 0) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

//! Convert a byte buffer to a reversed (high-index-first) lowercase hex
//! string, matching Bitcoin Core's uint256::GetHex() display order.
//! Bitcoin hashes are stored little-endian internally but rendered
//! big-endian; callers passing a `std::span<const uint8_t>` view of an
//! internally little-endian buffer get an RPC-style hex string back.
inline std::string HashHexStr(std::span<const uint8_t> bytes)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (size_t i = bytes.size(); i-- > 0;) {
        out.push_back(hex[(bytes[i] >> 4) & 0xF]);
        out.push_back(hex[bytes[i] & 0xF]);
    }
    return out;
}

//! Inverse of HashHexStr: parse a big-endian hex string and return the
//! reversed (little-endian) byte buffer suitable for an internally
//! little-endian hash type. Returns std::nullopt on any non-hex
//! character or odd length.
inline std::optional<std::vector<uint8_t>> TryParseHashHex(std::string_view hex)
{
    auto bytes = TryParseHex(hex);
    if (!bytes) return std::nullopt;
    std::reverse(bytes->begin(), bytes->end());
    return bytes;
}

} // namespace util

#endif // BITCOIN_TUI_UTIL_STRENCODINGS_H
