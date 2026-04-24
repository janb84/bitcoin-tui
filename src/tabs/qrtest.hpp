#pragma once

#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "elements/footer_spec.hpp"
#include "state.hpp"
#include "tabs/tab.hpp"

class QrTestTab : public Tab {
  public:
    using Tab::Tab;
    ~QrTestTab() override = default;

    std::string    name() const override { return "QR Test"; }
    ftxui::Element render(const AppState& snap) override;
    FooterSpec     footer_buttons(const AppState& snap) override;
    bool           handle_focused_event(const ftxui::Event& event) override;
    void           join() override {}

  private:
    std::string input_str_;
    bool        input_active_ = false;
    bool        qr_visible_   = false;
};
