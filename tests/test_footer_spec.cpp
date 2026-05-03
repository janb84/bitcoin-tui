#include "components/footer_spec.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FooterButton defaults", "[footer_spec]") {
    FooterButton btn{"label", nullptr};
    CHECK(btn.label == "label");
    CHECK(btn.on_click == nullptr);
    CHECK(btn.yellow == false);
}

TEST_CASE("FooterButton yellow flag", "[footer_spec]") {
    FooterButton btn{" [q] quit ", nullptr, true};
    CHECK(btn.yellow == true);
}

TEST_CASE("FooterSpec defaults show_search and show_quit", "[footer_spec]") {
    FooterSpec spec;
    CHECK(spec.show_search == true);
    CHECK(spec.show_quit == true);
    CHECK(spec.buttons.empty());
}

TEST_CASE("FooterSpec with buttons, default flags", "[footer_spec]") {
    FooterSpec spec{
        {{"  refresh", nullptr}, {"  [q] quit ", nullptr, true}},
    };
    CHECK(spec.buttons.size() == 2);
    CHECK(spec.show_search == true);
    CHECK(spec.show_quit == true);
}

TEST_CASE("FooterSpec hide search", "[footer_spec]") {
    FooterSpec spec{{}, false, true};
    CHECK(spec.show_search == false);
    CHECK(spec.show_quit == true);
}

TEST_CASE("FooterSpec hide quit", "[footer_spec]") {
    FooterSpec spec{{}, true, false};
    CHECK(spec.show_search == true);
    CHECK(spec.show_quit == false);
}

TEST_CASE("FooterSpec hide both", "[footer_spec]") {
    FooterSpec spec{{}, false, false};
    CHECK(spec.show_search == false);
    CHECK(spec.show_quit == false);
}
