#pragma once

#include <deque>
#include <utility>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

namespace components {

// Captures the on-screen rectangle of each rendered element (a list row, a
// footer button, …) so pointer clicks can be mapped back to a caller-supplied
// index. Use it to make element groups mouse-clickable.
//
// Usage (inside a render pass), for each clickable element:
//   hits.clear();                       // drop last frame's rectangles
//   for (int i = first; i < last; ++i)
//       elems.push_back(hits.track(make_element(i), i));
//
// Then, in the event handler:
//   if (int idx = hits.hit(mouse.x, mouse.y); idx >= 0) { ... }
//
// All members are single-thread (the UI/render thread): track() is called while
// building the element tree, hit() while dispatching events. A std::deque backs
// the rectangles so the references handed to ftxui::reflect() stay valid as more
// elements are tracked
class HitList {
  public:
    // Discard all rectangles. Call at the start of a render frame, or when the
    // group renders empty so a stale rectangle can't be clicked.
    void clear() { boxes_.clear(); }

    // Wrap an element so ftxui records its drawn rectangle, tagged with the
    // caller's index. Returns the decorated element to place in the tree.
    ftxui::Element track(ftxui::Element element, int index) {
        boxes_.push_back({ftxui::Box{}, index});
        return std::move(element) | ftxui::reflect(boxes_.back().first);
    }

    // Return the index whose rectangle contains (x, y), or -1 if none.
    int hit(int x, int y) const {
        for (const auto& [box, index] : boxes_) {
            if (box.Contain(x, y))
                return index;
        }
        return -1;
    }

  private:
    std::deque<std::pair<ftxui::Box, int>> boxes_;
};

} // namespace components
