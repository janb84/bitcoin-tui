#include <string>
#include <utility>

#include <ftxui/ftxui.hpp>

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

Element build_titled_panel(const std::string& title, const std::string& right_label, Elements rows,
                           int width, Color title_color) {
    Elements header = {
        text(title) | bold | color(title_color),
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
