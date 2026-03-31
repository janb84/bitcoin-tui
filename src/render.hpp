#pragma once

#include <cstddef>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "state.hpp"

// Shared helpers used by render_* functions and tab classes
ftxui::Element section_box(const std::string& title, ftxui::Elements rows);
ftxui::Element label_value(const std::string& lbl, const std::string& val,
                           ftxui::Color val_color = ftxui::Color::Default);
ftxui::Element mempool_stats_box(const AppState& s);

struct WindowSlice {
    int top = 0;
    int win = 0;
};

std::string    ellipsize_middle(const std::string& value, size_t max_len, size_t prefix,
                                size_t suffix);
WindowSlice    centered_window(int count, int selected, int max_visible);
ftxui::Element build_titled_panel(std::string title, const std::string& right_label,
                                  ftxui::Elements rows, int width,
                                  ftxui::Color title_color = ftxui::Color::Gold1);
ftxui::Element center_overlay(ftxui::Element body);
