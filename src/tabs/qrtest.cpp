#include "qrtest.hpp"

#include "elements/qr_item.hpp"
#include "elements/qr_overlay.hpp"
#include "render.hpp"

using namespace ftxui;

static Element render_qrtest_base(const std::string& input_str, bool input_active) {
    Elements rows;
    rows.push_back(text(""));

    auto input_field = hbox({
        text("  Input: ") | color(Color::GrayDark),
        text(input_str) | color(Color::White),
        input_active ? (text("│") | color(Color::White)) : text(""),
        filler(),
    });
    rows.push_back(input_active ? (input_field | inverted) : input_field);

    rows.push_back(text(""));
    if (input_active) {
        rows.push_back(text("  [r] / [⏎] generate QR   [Esc] cancel") | color(Color::GrayDark));
    } else {
        rows.push_back(text("  [i] / [⏎] edit input   [r] show QR") | color(Color::GrayDark));
    }

    Elements layout;
    layout.push_back(section_box("QR Code Test", rows));
    layout.push_back(filler());
    return vbox(std::move(layout)) | flex;
}

Element QrTestTab::render(const AppState& /*snap*/) {
    auto base = render_qrtest_base(input_str_, input_active_);
    if (qr_visible_ && !input_str_.empty()) {
        QrItems items{{"", input_str_}};
        return dbox({base, qr_overlay_element(items)});
    }
    return base;
}

FooterSpec QrTestTab::footer_buttons(const AppState& /*snap*/) {
    if (qr_visible_) {
        return FooterSpec{{{"  [Esc] close QR ",
                            [this] {
                                qr_visible_ = false;
                                screen_.PostEvent(Event::Custom);
                            },
                            true}}};
    }
    if (input_active_) {
        return FooterSpec{{
            {"  [r/⏎] show QR ",
             [this] {
                 if (!input_str_.empty()) {
                     qr_visible_   = true;
                     input_active_ = false;
                     screen_.PostEvent(Event::Custom);
                 }
             }},
            {"  [Esc] cancel ",
             [this] {
                 input_active_ = false;
                 screen_.PostEvent(Event::Custom);
             },
             true},
        }};
    }
    return FooterSpec{{
        {"  [i/⏎] edit input ",
         [this] {
             input_active_ = true;
             screen_.PostEvent(Event::Custom);
         }},
        {"  [r] show QR ",
         [this] {
             if (!input_str_.empty()) {
                 qr_visible_ = true;
                 screen_.PostEvent(Event::Custom);
             }
         }},
    }};
}

bool QrTestTab::handle_focused_event(const Event& event) {
    if (qr_visible_) {
        if (event == Event::Escape) {
            qr_visible_ = false;
            screen_.PostEvent(Event::Custom);
        }
        return true; // swallow all keys while overlay is visible
    }

    if (input_active_) {
        if (event == Event::Escape) {
            input_active_ = false;
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Return || event == Event::Character('r')) {
            if (!input_str_.empty()) {
                qr_visible_   = true;
                input_active_ = false;
            }
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Backspace) {
            if (!input_str_.empty())
                input_str_.pop_back();
            screen_.PostEvent(Event::Custom);
            return true;
        }
        if (event == Event::Tab || event == Event::TabReverse || event == Event::ArrowLeft ||
            event == Event::ArrowRight)
            return true;
        if (event.is_character()) {
            input_str_ += event.character();
            screen_.PostEvent(Event::Custom);
            return true;
        }
        return false;
    }

    if (event == Event::Character('i') || event == Event::Return) {
        input_active_ = true;
        screen_.PostEvent(Event::Custom);
        return true;
    }
    if (event == Event::Character('r')) {
        if (!input_str_.empty()) {
            qr_visible_ = true;
            screen_.PostEvent(Event::Custom);
        }
        return true;
    }
    return false;
}
