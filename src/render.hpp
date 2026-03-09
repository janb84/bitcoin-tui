#pragma once

#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "state.hpp"

// Shared label/value row used by both render_* functions and the overlay in main.cpp
ftxui::Element label_value(const std::string& lbl, const std::string& val,
                           ftxui::Color val_color = ftxui::Color::Default);

ftxui::Element render_dashboard(const AppState& s);
ftxui::Element render_mempool(const AppState& s, int mempool_sel = -1);
ftxui::Element render_network(const AppState& s);
ftxui::Element render_peers(const AppState& s, int selected = -1);
ftxui::Element render_peer_detail(const PeerInfo&         p,
                                  const PeerActionResult& action = PeerActionResult{}, int sel = 0);
ftxui::Element render_addnode_overlay(const AddNodeState& addnode, const std::string& addr_str);
ftxui::Element render_ban_overlay(const BanNodeState& ban, const std::string& addr_str);
ftxui::Element render_added_nodes_panel(const std::vector<AddedNodeInfo>& nodes, bool loading,
                                        int selected);
ftxui::Element render_ban_list_panel(const std::vector<BannedEntry>& entries, bool loading,
                                     int selected);
ftxui::Element render_tools(const AppState& snap, const BroadcastState& bs, bool input_active,
                            const std::string& hex_str, int tools_sel);
