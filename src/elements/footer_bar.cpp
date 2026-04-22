#include "footer_bar.hpp"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace elements {

class FooterBarBase : public ComponentBase {
    std::function<FooterSpec()> spec_provider_;
    std::function<bool()>       is_search_active_;
    std::function<void()>       on_search_;
    std::function<void()>       on_quit_;

    std::vector<Box>          boxes_;
    std::vector<FooterButton> btns_;
    int                       hovered_ = -1;

  public:
    FooterBarBase(std::function<FooterSpec()> spec_provider, std::function<bool()> is_search_active,
                  std::function<void()> on_search, std::function<void()> on_quit)
        : spec_provider_(std::move(spec_provider)), is_search_active_(std::move(is_search_active)),
          on_search_(std::move(on_search)), on_quit_(std::move(on_quit)) {}

    Element OnRender() override {
        FooterSpec spec = spec_provider_();
        btns_.clear();

        if (is_search_active_()) {
            btns_.push_back({"  [Enter] search", nullptr, true});
            btns_.push_back({"  [Esc] cancel ", nullptr, true});
        } else {
            btns_ = std::move(spec.buttons);
            btns_.push_back({"  [Tab/←/→] switch ", nullptr});
            if (spec.show_search)
                btns_.push_back({" / search ", on_search_});
        }
        if (spec.show_quit)
            btns_.push_back({" [q] quit ", on_quit_});

        boxes_.assign(btns_.size(), Box{});
        Elements elems;
        for (int i = 0; i < static_cast<int>(btns_.size()); ++i) {
            const auto& b = btns_[i];
            Color       c = b.yellow ? Color::Yellow : Color::GrayDark;
            if (b.on_click && i == hovered_)
                c = Color::White;
            elems.push_back(text(b.label) | color(c) | reflect(boxes_[i]));
        }
        return hbox(std::move(elems));
    }

    bool OnEvent(Event event) override {
        if (!event.is_mouse())
            return false;
        auto& me = const_cast<Event&>(event);
        int   mx = me.mouse().x, my = me.mouse().y;
        int   new_hov = -1;
        for (int i = 0; i < static_cast<int>(boxes_.size()); ++i) {
            if (btns_[i].on_click && boxes_[i].Contain(mx, my)) {
                new_hov = i;
                break;
            }
        }
        hovered_ = new_hov;
        if (me.mouse().button == Mouse::Left && me.mouse().motion == Mouse::Released &&
            new_hov >= 0) {
            btns_[new_hov].on_click();
            return true;
        }
        return false;
    }
};

} // namespace elements

Component make_footer_bar(std::function<FooterSpec()> spec_provider,
                          std::function<bool()> is_search_active, std::function<void()> on_search,
                          std::function<void()> on_quit) {
    return Make<elements::FooterBarBase>(std::move(spec_provider), std::move(is_search_active),
                                         std::move(on_search), std::move(on_quit));
}
