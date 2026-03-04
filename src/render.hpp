#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "state.hpp"

// Shared label/value row used by both render_* functions and the overlay in main.cpp
ftxui::Element label_value(const std::string& lbl, const std::string& val,
                           ftxui::Color val_color = ftxui::Color::Default);

ftxui::Element render_dashboard(const AppState& s);
ftxui::Element render_mempool(const AppState& s);
ftxui::Element render_network(const AppState& s);
ftxui::Element render_peers(const AppState& s);
ftxui::Element render_tools(const AppState& snap, const BroadcastState& bs, const PsbtState& ps,
                            bool input_active, bool psbt_input_active,
                            const std::string& hex_str, const std::string& psbt_str,
                            int tools_sel);
