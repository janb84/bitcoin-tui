// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_TYPES_H
#define BITCOIN_TUI_INTERFACES_TYPES_H

// Minimal stand-ins for a few Bitcoin Core types that appear in the IPC
// interface signatures we expose. They implement just enough of the upstream
// API for libmultiprocess's type adapters to (de)serialize the corresponding
// capnp fields — no consensus / serialization framework / UniValue is
// pulled in.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

// 32-byte hash. Bitcoin Core's full uint256 is far more featureful, but the
// libmultiprocess `Data` adapter only needs `data()`, `size()`, `begin()`,
// `end()`, plus a `(const T*, const T*)` constructor (see
// IsByteSpan in mp/type-data.h).
class uint256
{
public:
    using value_type = unsigned char;
    static constexpr std::size_t WIDTH = 32;

    uint256() { m_data.fill(0); }

    // Required by mp/type-data.h's IsByteSpan concept.
    uint256(const unsigned char* begin, const unsigned char* end)
    {
        const std::size_t n = (end > begin) ? static_cast<std::size_t>(end - begin) : 0;
        const std::size_t copy_n = n < WIDTH ? n : WIDTH;
        std::memcpy(m_data.data(), begin, copy_n);
        if (copy_n < WIDTH) std::memset(m_data.data() + copy_n, 0, WIDTH - copy_n);
    }

    [[nodiscard]] const unsigned char* data() const noexcept { return m_data.data(); }
    [[nodiscard]] unsigned char* data() noexcept { return m_data.data(); }
    [[nodiscard]] static constexpr std::size_t size() noexcept { return WIDTH; }
    [[nodiscard]] auto begin() const noexcept { return m_data.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_data.end(); }
    [[nodiscard]] auto begin() noexcept { return m_data.begin(); }
    [[nodiscard]] auto end() noexcept { return m_data.end(); }

    // Hex-encode in big-endian display order (most significant byte first),
    // matching Bitcoin Core's GetHex() / ToString().
    [[nodiscard]] std::string ToString() const
    {
        static const char* hex = "0123456789abcdef";
        std::string out(WIDTH * 2, '0');
        for (std::size_t i = 0; i < WIDTH; ++i) {
            const unsigned char b = m_data[WIDTH - 1 - i];
            out[2 * i]     = hex[b >> 4];
            out[2 * i + 1] = hex[b & 0xf];
        }
        return out;
    }

    bool operator==(const uint256& other) const noexcept
    {
        return std::memcmp(m_data.data(), other.m_data.data(), WIDTH) == 0;
    }
    bool operator!=(const uint256& other) const noexcept { return !(*this == other); }

    [[nodiscard]] bool IsNull() const noexcept
    {
        for (auto b : m_data)
            if (b) return false;
        return true;
    }

private:
    std::array<unsigned char, WIDTH> m_data{};
};

// std::chrono alias used by interfaces::Mining::waitTipChanged. Matches
// Bitcoin Core's util/time.h definition exactly so the wire format is
// identical.
using MillisecondsDouble = std::chrono::duration<double, std::chrono::milliseconds::period>;

namespace interfaces {

// Mirror of upstream interfaces::BlockRef: hash + height of a block. The
// capnp `BlockRef` struct in mining.capnp wraps this 1:1 by field name.
struct BlockRef
{
    uint256 hash;
    int     height = -1;
};

} // namespace interfaces

// Parse a 64-char hex hash (display order, MSB first) into a uint256
// (internal little-endian byte order). Returns nullopt on malformed input.
inline std::optional<uint256> hex_to_uint256(const std::string& hex)
{
    if (hex.size() != 64) return std::nullopt;
    uint256 out;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    };
    for (std::size_t i = 0; i < uint256::WIDTH; ++i) {
        const int hi = nibble(hex[2 * i]);
        const int lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.data()[uint256::WIDTH - 1 - i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return out;
}

#endif // BITCOIN_TUI_INTERFACES_TYPES_H
