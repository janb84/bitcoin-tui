#pragma once

#include <string_view>

#include <ftxui/dom/elements.hpp>

// Renders a Bitcoin address with alternating bold groups of 4 characters.
inline ftxui::Element address_element(std::string_view addr) {
    using namespace ftxui;
    if (addr.empty())
        return text("N/A");
    Elements parts;
    for (size_t i = 0; i < addr.size(); i += 4) {
        std::string chunk = (i > 0 ? " " : "") + std::string(addr.substr(i, 4));
        auto        e     = text(std::move(chunk));
        if ((i / 4) % 2 == 0)
            e = e | bold;
        else
            e = e | dim;
        parts.push_back(std::move(e));
    }
    return hbox(std::move(parts));
}
