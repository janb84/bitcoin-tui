#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <ftxui/ftxui.hpp>

#include "components/hit_list.hpp"

// Composable "recent blocks" visualization: one column per block, a vertical
// fill bar on top (colored by how full the block is), the block label below
// (inverted when selected) and gray sub-lines under it. Exposed to Lua tabs
// via btcui_blocks(); the slide animation moves the columns one slot to the
// right when a new block arrives (the caller passes the pre-arrival snapshot
// and a progress fraction while the animation runs).

namespace components {

struct BlockBar {
    std::string              key;   // activation key (fired via btcui_on_select)
    std::string              label; // e.g. the formatted height
    double                   fill = 0.0;
    std::vector<std::string> lines; // gray sub-lines (tx count, size, age, …)
};

// `anim_old` + `progress` in [0,1): slide phase — render the pre-arrival blocks
// minus the last one, padded left so the row glides one column to the right.
// `hits` (optional) records each column's screen rectangle for mouse clicks;
// nothing is tracked while the slide runs (the columns are mid-flight).
inline ftxui::Element blockbars_element(const std::vector<BlockBar>& blocks, int selected,
                                        const std::vector<BlockBar>* anim_old = nullptr,
                                        double progress = 0.0, HitList* hits = nullptr) {
    using namespace ftxui;
    constexpr int BAR_HEIGHT = 6;
    constexpr int COL_WIDTH  = 10;

    bool                         slide = anim_old != nullptr && !anim_old->empty();
    const std::vector<BlockBar>& src   = slide ? *anim_old : blocks;
    int                          num   = static_cast<int>(src.size());
    int max_cols                       = std::max(1, (Terminal::Size().dimx - 4) / (COL_WIDTH + 1));
    int max_render                     = std::min(slide ? std::max(0, num - 1) : num, max_cols);

    // Slide offset grows from 0 → (COL_WIDTH+1) chars over the animation.
    int left_pad = 0;
    if (slide)
        left_pad = static_cast<int>(std::round(std::clamp(progress, 0.0, 1.0) * (COL_WIDTH + 1)));

    Elements block_cols;
    for (int i = 0; i < max_render; ++i) {
        const auto& b    = src[i];
        double      fill = std::clamp(b.fill, 0.0, 1.0);

        Color bar_color = fill > 0.9   ? Color(Color::DarkOrange)
                          : fill > 0.7 ? Color(Color::Yellow)
                                       : Color(Color::Green);

        int filled_rows = static_cast<int>(std::round(fill * BAR_HEIGHT));

        Elements bar;
        for (int r = 0; r < BAR_HEIGHT; ++r) {
            bool is_filled = r >= (BAR_HEIGHT - filled_rows);
            bar.push_back(is_filled ? text("██████████") | color(bar_color)
                                    : text("░░░░░░░░░░") | color(Color::GrayDark));
        }

        if (!block_cols.empty())
            block_cols.push_back(text(" "));

        bool     is_selected = (i == selected);
        Elements col;
        col.push_back(vbox(std::move(bar)));
        col.push_back(is_selected ? text(b.label) | center | inverted | bold
                                  : text(b.label) | center);
        for (const auto& line : b.lines)
            col.push_back(text(line) | center | color(Color::GrayDark));
        auto col_el = vbox(std::move(col)) | size(WIDTH, EQUAL, COL_WIDTH);
        if (hits && !slide)
            col_el = hits->track(std::move(col_el), i);
        block_cols.push_back(std::move(col_el));
    }

    return left_pad > 0 ? hbox({text(std::string(left_pad, ' ')), hbox(std::move(block_cols))})
                        : hbox(std::move(block_cols));
}

} // namespace components
