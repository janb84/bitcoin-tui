#pragma once

#include <algorithm>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

// Renders a horizontal progress bar: an optional bold prefix label, a flexing
// gauge that fills the available width, and a trailing percentage. The frac is
// clamped to [0, 1]. Used by the dashboard (sync / memory usage rows) and
// exposed to Lua tabs via btcui_gauge().
inline ftxui::Element gauge_element(double frac, ftxui::Color bar_color,
                                    const std::string& prefix = "") {
    using namespace ftxui;
    float    f   = static_cast<float>(std::clamp(frac, 0.0, 1.0));
    int      pct = static_cast<int>(f * 100);
    Elements parts;
    if (!prefix.empty()) {
        parts.push_back(text(prefix) | bold);
        parts.push_back(text("  "));
    }
    parts.push_back(gauge(f) | flex | color(bar_color));
    parts.push_back(text(" " + std::to_string(pct) + "%") | bold | color(bar_color));
    // xflex so the component grows to fill its parent's width; without it the
    // surrounding hbox sizes us to natural width and the inner gauge collapses
    // to ~1 cell (100% shows a single block, smaller values show nothing).
    return hbox(std::move(parts)) | xflex;
}
