#pragma once
#include "footer_spec.hpp"
#include <ftxui/component/component.hpp>
#include <functional>

// on_search / on_quit are permanent callbacks set once at construction.
// is_search_active is queried each render to switch between normal and
// search-active mode. The footer bar owns all button-building logic.
ftxui::Component make_footer_bar(std::function<FooterSpec()> spec_provider,
                                 std::function<bool()>       is_search_active,
                                 std::function<void()> on_search, std::function<void()> on_quit);
