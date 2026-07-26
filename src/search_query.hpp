#pragma once

#include <algorithm>
#include <cctype>
#include <string>

// Predicates for the "/" search bar in main: the host only dispatches a query to
// the tab that registered btcui_on_search when it looks like something a node can
// be asked about (a 64-hex txid/block hash, or a block height).
inline bool is_txid(const std::string& s) {
    if (s.size() != 64)
        return false;
    return std::ranges::all_of(s, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

inline bool is_height(const std::string& s) {
    return !s.empty() && s.size() <= 8 &&
           std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c) != 0; });
}
