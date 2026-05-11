// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TUI_INTERFACES_TYPES_H
#define BITCOIN_TUI_INTERFACES_TYPES_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace interfaces {

// Stand-in for Bitcoin Core's uint256, just enough to satisfy
// libmultiprocess's `Data` adapter, which (per mp/type-data.h) requires:
//   - implicit convertibility to std::span<const U> for some byte type U,
//   - a (const U*, const U*) byte-range constructor,
//   - begin()/end() iterators yielding bytes (used to deduce U).
class uint256
{
public:
    static constexpr size_t WIDTH = 32;

    uint256() { m_data.fill(0); }
    uint256(const uint8_t* begin, const uint8_t* end)
    {
        const size_t len = static_cast<size_t>(end - begin);
        // Accept the empty-Data case (length 0) and treat it as a null
        // uint256. v31.0's Mining wire format has no `hasResult` companion
        // for waitTipChanged, so the server signals "no result" (interrupt /
        // shutdown) by sending a zero-length BlockRef.hash, which would
        // otherwise trip up libmultiprocess's read path.
        if (len == 0) { m_data.fill(0); return; }
        if (len != WIDTH) {
            throw std::runtime_error("uint256: byte range has wrong length");
        }
        std::copy(begin, end, m_data.begin());
    }

    uint8_t* data()             { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }
    size_t size() const         { return m_data.size(); }

    using iterator = uint8_t*;
    using const_iterator = const uint8_t*;
    iterator       begin()       { return m_data.data(); }
    iterator       end()         { return m_data.data() + WIDTH; }
    const_iterator begin() const { return m_data.data(); }
    const_iterator end()   const { return m_data.data() + WIDTH; }

    // Required by libmultiprocess's IsByteSpan concept (mp/type-data.h).
    operator std::span<const uint8_t>() const { return {m_data.data(), WIDTH}; }

    bool IsNull() const
    {
        return std::all_of(m_data.begin(), m_data.end(), [](uint8_t b) { return b == 0; });
    }

    bool operator==(const uint256& other) const { return m_data == other.m_data; }
    bool operator!=(const uint256& other) const { return !(*this == other); }

private:
    std::array<uint8_t, WIDTH> m_data{};
};

// 1:1 wrap of upstream BlockRef. Field names must match the capnp schema's
// $Proxy.wrap so the generated proxy code can read/write {hash, height}.
struct BlockRef
{
    uint256 hash;
    int32_t height = 0;
};

// Matches the type used in upstream interfaces::Mining::waitTipChanged.
using MillisecondsDouble = std::chrono::duration<double, std::milli>;

} // namespace interfaces

#endif // BITCOIN_TUI_INTERFACES_TYPES_H
