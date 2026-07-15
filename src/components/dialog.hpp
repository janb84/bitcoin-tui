#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/ftxui.hpp>

#include "components/hit_list.hpp"
#include "render.hpp"

// Composable modal dialog overlay, exposed to Lua tabs via btcui_dialog().
// A dialog is a centered titled panel built from declarative parts:
//   rows     — text / label:value / separator / selectable item rows
//   choice   — an optional value cycled with ←/→ (e.g. onetry∕add)
//   input    — an optional single-line text field (typed on the UI thread)
//   buttons  — an optional ←/→ selectable button row activated with Enter or click
//   hint     — an optional gray key-hint line at the bottom; its "[key] label"
//              segments are clickable and act like pressing the key
// The UI thread owns navigation state (item_sel / button_sel / input buffer);
// activations are queued as DialogEvents and dispatched to the Lua thread.

namespace components {

// Map a Lua color name to an FTXUI color, falling back when unset/unknown.
inline ftxui::Color lua_color(const std::string& name, ftxui::Color fallback) {
    using ftxui::Color;
    if (name == "red")
        return Color::Red;
    if (name == "green")
        return Color::Green;
    if (name == "yellow")
        return Color::Yellow;
    if (name == "cyan")
        return Color::Cyan;
    if (name == "white")
        return Color::White;
    if (name == "gray")
        return Color::GrayDark;
    return fallback;
}

struct DialogSpan {
    std::string text;
    std::string color; // Lua color name; empty = default
    bool        bold = false;
};

struct DialogRow {
    enum class Kind { Text, LabelValue, Separator, Item };
    Kind                    kind = Kind::Text;
    std::string             label, value, value_color; // LabelValue
    std::vector<DialogSpan> spans;                     // Text & Item content
    std::string             right, right_color;        // optional right-aligned text
    std::string             key;                       // Item activation key
};

struct DialogChoice {
    std::string              label;
    std::vector<std::string> options;
    int                      index = 0; // 0-based
};

struct DialogInput {
    std::string label;
    std::string buffer;
    bool        done = false; // true after submit: stop typing, hide cursor
};

struct DialogState {
    bool                        active = false;
    std::string                 title;
    int                         width = 64;
    std::vector<DialogRow>      rows;
    std::optional<DialogChoice> choice;
    std::optional<DialogInput>  input;
    std::vector<std::string>    buttons;
    std::string                 hint;
    bool                        closable = true; // false: Esc is swallowed
    // UI-thread navigation state
    int button_sel = 0;
    int item_sel   = -1; // ordinal among Item rows, -1 = none
    // Item keys in row order, for event dispatch (filled while parsing).
    std::vector<std::string> item_keys;
    // Mouse hover highlight, tracked by the UI thread from pointer motion.
    enum class HoverKind { None, Button, Item, Hint };
    HoverKind hover_kind  = HoverKind::None;
    int       hover_index = -1;
};

// Screen rectangles captured while rendering a dialog, so the UI thread can
// hit-test mouse clicks against buttons, item rows and hint segments.
// UI-thread only.
struct DialogHits {
    ftxui::Box panel;       // whole dialog rectangle
    ftxui::Box items_frame; // visible window of the (scrollable) item list
    HitList    items;       // selectable Item rows, tagged with their ordinal
    HitList    buttons;     // button row entries, tagged with their index
    // "[key] label" segments of the hint line: clicking one acts like pressing
    // the key. hint_keys[i] is the bracketed key of the segment tagged i.
    HitList                  hint;
    std::vector<std::string> hint_keys;
    void                     clear() {
        panel       = {};
        items_frame = {};
        items.clear();
        buttons.clear();
        hint.clear();
        hint_keys.clear();
    }
};

// A row activation/keypress queued by the UI thread for the Lua thread.
struct DialogEvent {
    std::string type;       // "submit" | "select" | "button" | "key" | "close"
    std::string text;       // submit: input buffer
    std::string key;        // select: item key; key: pressed character/arrow
    std::string label;      // button: button label
    int         index  = 0; // button: 1-based button index
    int         choice = 0; // submit: 1-based choice index (0 = no choice line)
};

inline ftxui::Element dialog_spans(const std::vector<DialogSpan>& spans) {
    using namespace ftxui;
    Elements parts;
    for (const auto& s : spans) {
        auto e = text(s.text);
        if (!s.color.empty())
            e = e | color(lua_color(s.color, Color::Default));
        if (s.bold)
            e = e | bold;
        parts.push_back(std::move(e));
    }
    return hbox(std::move(parts));
}

inline ftxui::Element dialog_element(const DialogState& d, DialogHits* hits = nullptr) {
    using namespace ftxui;

    if (hits)
        hits->clear();

    Elements rows;
    bool     has_items    = false;
    int      item_ordinal = 0;
    for (const auto& row : d.rows) {
        Element el;
        switch (row.kind) {
        case DialogRow::Kind::Separator:
            rows.push_back(separator());
            continue;
        case DialogRow::Kind::LabelValue:
            el = label_value(row.label, row.value, lua_color(row.value_color, Color::Default));
            break;
        case DialogRow::Kind::Text:
        case DialogRow::Kind::Item:
            el = dialog_spans(row.spans);
            break;
        }
        if (!row.right.empty()) {
            auto r = text(row.right);
            if (!row.right_color.empty())
                r = r | color(lua_color(row.right_color, Color::Default));
            el = hbox({std::move(el) | flex, std::move(r), text(" ")});
        } else {
            el = hbox({std::move(el), filler()});
        }
        if (row.kind == DialogRow::Kind::Item) {
            has_items = true;
            if (item_ordinal == d.item_sel)
                el = std::move(el) | inverted | focus;
            else if (d.hover_kind == DialogState::HoverKind::Item && d.hover_index == item_ordinal)
                el = std::move(el) | bgcolor(Color::GrayDark);
            if (hits)
                el = hits->items.track(std::move(el), item_ordinal);
            ++item_ordinal;
        }
        rows.push_back(std::move(el));
    }
    if (has_items) {
        // Long item lists scroll, keeping the selected row visible. The frame box
        // is captured so clicks on rows scrolled out of view don't hit-test.
        auto body = vbox(std::move(rows)) | yframe;
        if (hits)
            body = std::move(body) | reflect(hits->items_frame);
        rows = {std::move(body)};
    }

    if (d.choice) {
        rows.push_back(hbox({
            text("  " + d.choice->label + " : ") | color(Color::GrayDark),
            text(d.choice->index >= 0 &&
                         d.choice->index < static_cast<int>(d.choice->options.size())
                     ? d.choice->options[d.choice->index]
                     : "") |
                color(Color::Yellow) | bold,
            text("  [←/→ to change]") | color(Color::GrayDark),
            filler(),
        }));
    }

    if (d.input) {
        Elements parts = {
            text("  " + d.input->label + " : ") | color(Color::GrayDark),
            text(d.input->buffer) | color(Color::White),
        };
        if (!d.input->done)
            parts.push_back(text("│") | color(Color::White));
        parts.push_back(filler());
        rows.push_back(hbox(std::move(parts)));
    }

    if (!d.buttons.empty()) {
        rows.push_back(separator());
        Elements btns;
        for (int i = 0; i < static_cast<int>(d.buttons.size()); ++i) {
            auto e = text("  " + d.buttons[i] + "  ");
            if (i == d.button_sel)
                e = std::move(e) | inverted;
            else if (d.hover_kind == DialogState::HoverKind::Button && d.hover_index == i)
                e = std::move(e) | bgcolor(Color::GrayDark);
            if (hits)
                e = hits->buttons.track(std::move(e), i);
            btns.push_back(std::move(e));
            btns.push_back(text("  "));
        }
        btns.push_back(filler());
        rows.push_back(hbox(std::move(btns)));
    }

    if (!d.hint.empty()) {
        rows.push_back(text(""));
        if (hits) {
            // Split the hint into clickable "[key] label" segments so a click on
            // one acts like pressing the key (trailing separator spaces between
            // segments stay untracked).
            Elements    parts{text("  ")};
            size_t      pos = 0;
            int         seg = 0;
            const auto& h   = d.hint;
            while (pos < h.size()) {
                size_t open = h.find('[', pos);
                if (open == std::string::npos) {
                    parts.push_back(text(h.substr(pos)));
                    break;
                }
                if (open > pos)
                    parts.push_back(text(h.substr(pos, open - pos)));
                size_t close = h.find(']', open);
                if (close == std::string::npos) {
                    parts.push_back(text(h.substr(open)));
                    break;
                }
                size_t next    = h.find('[', close + 1);
                size_t end     = (next == std::string::npos) ? h.size() : next;
                size_t seg_end = end;
                while (seg_end > close + 1 && h[seg_end - 1] == ' ')
                    --seg_end;
                hits->hint_keys.push_back(h.substr(open + 1, close - open - 1));
                auto seg_el = text(h.substr(open, seg_end - open));
                // A hovered segment lifts to white (the outer gray is overwritten
                // because the inner color node renders last).
                if (d.hover_kind == DialogState::HoverKind::Hint && d.hover_index == seg)
                    seg_el = std::move(seg_el) | color(Color::White);
                parts.push_back(hits->hint.track(std::move(seg_el), seg++));
                if (seg_end < end)
                    parts.push_back(text(h.substr(seg_end, end - seg_end)));
                pos = end;
            }
            parts.push_back(filler());
            rows.push_back(hbox(std::move(parts)) | color(Color::GrayDark));
        } else {
            rows.push_back(text("  " + d.hint) | color(Color::GrayDark));
        }
    }

    auto panel = build_titled_panel(" " + d.title + " ", "", std::move(rows), d.width);
    if (hits)
        panel = std::move(panel) | reflect(hits->panel);
    return center_overlay(std::move(panel));
}

} // namespace components
