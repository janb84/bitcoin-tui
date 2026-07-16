#pragma once

#include <cstddef>
#include <string>

#include <ftxui/ftxui.hpp>

// Shared helpers used by render_* functions and tab classes
ftxui::Element section_box(const std::string& title, ftxui::Elements rows);
ftxui::Element label_value(const std::string& lbl, const std::string& val,
                           ftxui::Color val_color = ftxui::Color::Default);

std::string    ellipsize_middle(const std::string& value, size_t max_len, size_t prefix,
                                size_t suffix);
ftxui::Element build_titled_panel(const std::string& title, const std::string& right_label,
                                  ftxui::Elements rows, int width,
                                  ftxui::Color title_color = ftxui::Color::Gold1);
ftxui::Element center_overlay(ftxui::Element body);
