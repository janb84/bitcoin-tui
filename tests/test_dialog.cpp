// Tests for the mouse hit-testing rectangles captured while rendering a
// btcui_dialog overlay (components::DialogHits): buttons, item rows and
// "[key] label" hint segments must map screen coordinates back to the right
// index, and nothing outside the dialog may hit.

#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/ftxui.hpp>

#include "components/dialog.hpp"

using namespace ftxui;

namespace {

// Locate an ASCII needle on the rendered screen, comparing cell by cell so
// multi-byte border glyphs don't shift the column offset.
std::pair<int, int> find_text(Screen& screen, const std::string& needle) {
    const int n = static_cast<int>(needle.size());
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x + n <= screen.dimx(); ++x) {
            bool match = true;
            for (int i = 0; i < n && match; ++i)
                match = screen.at(x + i, y) == std::string(1, needle[i]);
            if (match)
                return {x, y};
        }
    }
    return {-1, -1};
}

components::DialogRow make_item(const std::string& label, const std::string& key) {
    components::DialogRow row;
    row.kind  = components::DialogRow::Kind::Item;
    row.spans = {{" " + label, "", false}};
    row.key   = key;
    return row;
}

} // namespace

TEST_CASE("dialog_element fills DialogHits for buttons, items and hint") {
    components::DialogState d;
    d.active     = true;
    d.title      = "Test";
    d.width      = 44;
    d.rows       = {make_item("node-one", "one"), make_item("node-two", "two")};
    d.item_keys  = {"one", "two"};
    d.buttons    = {"OK", "Cancel"};
    d.hint       = "[a] add node  [Esc] close";
    d.button_sel = 0;

    components::DialogHits hits;
    Screen                 screen(80, 24);
    Render(screen, components::dialog_element(d, &hits));

    // Buttons hit-test to their index; the panel box contains them.
    auto [okx, oky] = find_text(screen, " OK ");
    REQUIRE(okx >= 0);
    CHECK(hits.panel.Contain(okx + 1, oky));
    CHECK(hits.buttons.hit(okx + 1, oky) == 0);
    auto [cax, cay] = find_text(screen, "Cancel");
    REQUIRE(cax >= 0);
    CHECK(hits.buttons.hit(cax, cay) == 1);

    // Item rows hit-test to their ordinal, inside the captured list frame.
    auto [i1x, i1y] = find_text(screen, "node-one");
    REQUIRE(i1x >= 0);
    CHECK(hits.items_frame.Contain(i1x, i1y));
    CHECK(hits.items.hit(i1x, i1y) == 0);
    auto [i2x, i2y] = find_text(screen, "node-two");
    REQUIRE(i2x >= 0);
    CHECK(hits.items.hit(i2x, i2y) == 1);

    // Hint segments: "[a] add node" maps to key "a", "[Esc] close" to "Esc";
    // the separator gap between segments is not clickable.
    REQUIRE(hits.hint_keys.size() == 2);
    CHECK(hits.hint_keys[0] == "a");
    CHECK(hits.hint_keys[1] == "Esc");
    auto [hax, hay] = find_text(screen, "[a] add node");
    REQUIRE(hax >= 0);
    CHECK(hits.hint.hit(hax, hay) == 0);
    CHECK(hits.hint.hit(hax + 11, hay) == 0);
    CHECK(hits.hint.hit(hax + 12, hay) == -1);
    auto [hex, hey] = find_text(screen, "[Esc] close");
    REQUIRE(hex >= 0);
    CHECK(hits.hint.hit(hex, hey) == 1);

    // The screen corner is outside the centered panel: nothing hits.
    CHECK_FALSE(hits.panel.Contain(0, 0));
    CHECK(hits.buttons.hit(0, 0) == -1);
    CHECK(hits.items.hit(0, 0) == -1);
    CHECK(hits.hint.hit(0, 0) == -1);
}

TEST_CASE("dialog_element applies the hover highlight") {
    components::DialogState d;
    d.active  = true;
    d.title   = "Hover";
    d.width   = 44;
    d.buttons = {"OK", "Cancel"};
    d.hint    = "[a] add node  [Esc] close";

    // Hovering the second hint segment lifts it to white; the first stays gray.
    d.hover_kind  = components::DialogState::HoverKind::Hint;
    d.hover_index = 1;
    {
        components::DialogHits hits;
        Screen                 screen(80, 24);
        Render(screen, components::dialog_element(d, &hits));
        auto [hex, hey] = find_text(screen, "[Esc] close");
        auto [hax, hay] = find_text(screen, "[a] add node");
        REQUIRE(hex >= 0);
        REQUIRE(hax >= 0);
        CHECK(screen.PixelAt(hex, hey).foreground_color == Color::White);
        CHECK(screen.PixelAt(hax, hay).foreground_color == Color::GrayDark);
    }

    // Hovering a non-selected button fills its background; the selected button
    // (button_sel 0) is inverted, so hovering button 1 is what we check.
    d.hover_kind  = components::DialogState::HoverKind::Button;
    d.hover_index = 1;
    d.button_sel  = 0;
    {
        components::DialogHits hits;
        Screen                 screen(80, 24);
        Render(screen, components::dialog_element(d, &hits));
        auto [cax, cay] = find_text(screen, "Cancel");
        REQUIRE(cax >= 0);
        CHECK(screen.PixelAt(cax, cay).background_color == Color::GrayDark);
    }
}

TEST_CASE("dialog_element without a hits pointer renders fine") {
    components::DialogState d;
    d.active  = true;
    d.title   = "Plain";
    d.width   = 30;
    d.buttons = {"Close"};
    d.hint    = "[Esc] close";

    Screen screen(60, 16);
    Render(screen, components::dialog_element(d));
    CHECK(find_text(screen, "Close").first >= 0);
}
