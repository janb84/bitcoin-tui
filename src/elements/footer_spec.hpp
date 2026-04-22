#pragma once
#include <functional>
#include <string>
#include <vector>

struct FooterButton {
    std::string           label;
    std::function<void()> on_click; // nullptr = display-only
    bool                  yellow = false;
};

struct FooterSpec {
    std::vector<FooterButton> buttons;
    bool                      show_search = true;
    bool                      show_quit   = true;
};
