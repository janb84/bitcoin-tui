#pragma once

#include <algorithm>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "elements/qr_item.hpp"
#include "render.hpp"
#include "vendor/qrcodegen.hpp"

// Renders a QR code (or tabbed set of QR codes) as a centered overlay panel.
// Uses Unicode half-block characters (▀▄█) to pack 2 vertical modules per
// terminal row, with a 2-module quiet zone on each side.
// When items.size() > 1, a tab bar is shown at the top; `selected` is clamped.
inline ftxui::Element qr_overlay_element(const QrItems& items, int selected = 0) {
    using namespace ftxui;

    if (items.empty())
        return center_overlay(build_titled_panel(" QR Code ", "", {text("  (no data)")}, 20));

    selected         = std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
    const auto& item = items[selected];

    qrcodegen::QrCode qr =
        qrcodegen::QrCode::encodeText(item.data.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

    int           sz         = qr.getSize();
    constexpr int quiet      = 2;
    int           total_cols = sz + 2 * quiet;

    Elements qr_rows;
    for (int y = -quiet; y < sz + quiet; y += 2) {
        std::string line;
        for (int x = -quiet; x < sz + quiet; x++) {
            bool in_x = (x >= 0 && x < sz);
            bool top  = in_x && y >= 0 && y < sz && qr.getModule(x, y);
            bool bot  = in_x && y + 1 >= 0 && y + 1 < sz && qr.getModule(x, y + 1);
            if (top && bot)
                line += "\xe2\x96\x88"; // █ full block
            else if (top)
                line += "\xe2\x96\x80"; // ▀ upper half
            else if (bot)
                line += "\xe2\x96\x84"; // ▄ lower half
            else
                line += " ";
        }
        qr_rows.push_back(text(std::move(line)));
    }

    auto qr_box = vbox(std::move(qr_rows)) | color(Color::Black) | bgcolor(Color::White);

    int tab_bar_width = 0;
    for (const auto& it : items)
        tab_bar_width += static_cast<int>(it.label.size()) + 2; // " label "

    int panel_width =
        std::max({total_cols + 4, static_cast<int>(item.data.size()) + 6, tab_bar_width + 4});

    Elements panel_rows;

    // Tab bar — only shown when there are multiple items
    if (items.size() > 1) {
        Elements tabs;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            auto t = text(" " + items[i].label + " ");
            tabs.push_back(i == selected ? std::move(t) | inverted : std::move(t) | dim);
        }
        panel_rows.push_back(hbox(std::move(tabs)));
        panel_rows.push_back(separator());
    }

    panel_rows.push_back(hbox({filler(), qr_box, filler()}));
    panel_rows.push_back(separator());
    panel_rows.push_back(hbox({text("  "), text(item.data) | color(Color::White), filler()}));

    auto panel = build_titled_panel(" QR Code ", "", std::move(panel_rows), panel_width);
    return center_overlay(std::move(panel));
}
