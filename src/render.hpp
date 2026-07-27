#pragma once

#include <string>

#include <ftxui/ftxui.hpp>

// Shared FTXUI building blocks used by the Lua tab host, its overlays and the
// app chrome in main.
ftxui::Element section_box(const std::string& title, ftxui::Elements rows);
ftxui::Element label_value(const std::string& lbl, const std::string& val,
                           ftxui::Color val_color = ftxui::Color::Default);
ftxui::Element build_titled_panel(const std::string& title, const std::string& right_label,
                                  ftxui::Elements rows, int width,
                                  ftxui::Color title_color = ftxui::Color::Gold1);
ftxui::Element center_overlay(ftxui::Element body);
