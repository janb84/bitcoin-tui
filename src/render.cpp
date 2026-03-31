#include <algorithm>
#include <string>
#include <utility>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "format.hpp"
#include "render.hpp"

using namespace ftxui;

Element section_box(const std::string& title, Elements rows) {
    Elements content;
    content.reserve(rows.size() + 1);
    content.push_back(text(" " + title + " ") | bold | color(Color::Gold1));
    for (auto& r : rows)
        content.push_back(std::move(r));
    return vbox(std::move(content)) | border;
}

std::string ellipsize_middle(const std::string& value, size_t max_len, size_t prefix,
                             size_t suffix) {
    if (value.size() <= max_len)
        return value;
    return value.substr(0, prefix) + "\u2026" + value.substr(value.size() - suffix);
}

WindowSlice centered_window(int count, int selected, int max_visible) {
    WindowSlice out;
    if (count <= 0)
        return out;
    out.win = std::min(count, max_visible);
    if (selected >= 0) {
        out.top = std::max(0, selected - out.win / 2);
        out.top = std::min(out.top, count - out.win);
    }
    return out;
}

Element build_titled_panel(std::string title, const std::string& right_label, Elements rows,
                           int width, Color title_color) {
    Elements header = {
        text(std::move(title)) | bold | color(title_color),
        filler(),
    };
    if (!right_label.empty())
        header.push_back(text(" " + right_label + " ") | color(Color::GrayDark));

    return vbox({hbox(std::move(header)), separator(), vbox(std::move(rows))}) | border |
           size(WIDTH, EQUAL, width);
}

Element center_overlay(Element body) {
    return vbox({filler(), hbox({filler(), std::move(body), filler()}), filler()}) | flex;
}

Element label_value(const std::string& lbl, const std::string& val, Color val_color) {
    return hbox({
        text(lbl) | color(Color::GrayDark),
        text(val) | color(val_color) | bold,
    });
}

Element mempool_stats_box(const AppState& s) {
    double usage_frac  = s.mempool_max > 0 ? static_cast<double>(s.mempool_usage) /
                                                static_cast<double>(s.mempool_max)
                                           : 0.0;
    Color  usage_color = usage_frac > 0.8   ? Color::Red
                         : usage_frac > 0.5 ? Color::Yellow
                                            : Color::Cyan;
    return section_box(
        "Mempool",
        {
            label_value("  Transactions    : ", fmt_int(s.mempool_tx)),
            label_value("  Virtual size    : ", fmt_bytes(s.mempool_bytes)),
            label_value("  Total fees      : ", fmt_btc(s.total_fee)),
            label_value("  Min relay fee   : ", fmt_satsvb(s.mempool_min_fee)),
            hbox({
                text("  Memory usage    : ") | color(Color::GrayDark),
                text(fmt_bytes(s.mempool_usage) + " / " + fmt_bytes(s.mempool_max)) | bold,
                text("  "),
                gauge(static_cast<float>(usage_frac)) | flex | color(usage_color),
                text(" " + std::to_string(static_cast<int>(usage_frac * 100)) + "%  ") | bold |
                    color(usage_color),
            }),
        });
}
