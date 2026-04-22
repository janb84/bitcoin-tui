#include "network.hpp"

#include <algorithm>

#include "format.hpp"
#include "render.hpp"

using namespace ftxui;

static Element render_network(const AppState& s, const std::vector<SoftFork>& forks,
                              bool forks_loading) {
    auto left_col = section_box(
        "Network Status", {
                              label_value("  Active    : ", s.network_active ? "yes" : "no",
                                          s.network_active ? Color::Green : Color::Red),
                              label_value("  Peers     : ", std::to_string(s.connections)),
                              label_value("  Inbound   : ", std::to_string(s.connections_in)),
                              label_value("  Outbound  : ", std::to_string(s.connections_out)),
                          });

    auto right_col =
        section_box("Node", {
                                label_value("  Client    : ", s.subversion),
                                label_value("  Protocol  : ", std::to_string(s.protocol_version)),
                                label_value("  Relay fee : ", fmt_satsvb(s.relay_fee)),
                            });

    Elements fork_rows;
    if (forks_loading) {
        fork_rows.push_back(text("  Loading\u2026") | color(Color::GrayDark));
    } else if (forks.empty()) {
        fork_rows.push_back(text("  (unavailable \u2014 node may not support getdeploymentinfo)") |
                            color(Color::GrayDark));
    } else {
        // Header
        fork_rows.push_back(hbox({
                                text("  "),
                                text("Name") | bold | size(WIDTH, EQUAL, 18),
                                text("Type") | bold | size(WIDTH, EQUAL, 8),
                                text("Status") | bold | size(WIDTH, EQUAL, 12),
                                text("Height") | bold,
                                filler(),
                            }) |
                            color(Color::Gold1));
        fork_rows.push_back(separator());
        for (const auto& f : forks) {
            const std::string& status       = f.bip9_status.empty() ? f.type : f.bip9_status;
            Color              status_color = f.active                ? Color::Green
                                              : status == "locked_in" ? Color::Yellow
                                              : status == "started"   ? Color::Cyan
                                                                      : Color::GrayDark;
            fork_rows.push_back(hbox({
                text("  "),
                text(f.name) | size(WIDTH, EQUAL, 18),
                text(f.type) | color(Color::GrayDark) | size(WIDTH, EQUAL, 8),
                text(status) | color(status_color) | size(WIDTH, EQUAL, 12),
                f.height >= 0 ? text(fmt_height(f.height)) | color(Color::GrayDark) : text("—"),
                filler(),
            }));
        }
    }

    return vbox({
               hbox({std::move(left_col) | flex, std::move(right_col) | flex}),
               section_box("Softfork Tracking", std::move(fork_rows)),
               filler(),
           }) |
           flex;
}

NetworkTab::NetworkTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                       std::atomic<bool>& running, Guarded<AppState>& state, int refresh_secs)
    : Tab(std::move(cfg), auth, screen, running, state, refresh_secs) {}

FooterSpec NetworkTab::footer_buttons(const AppState& snap) {
    return FooterSpec{{refresh_btn(snap)}};
}

void NetworkTab::fetch() {
    if (loading_.load())
        return;
    if (thread_.joinable())
        thread_.join();
    loading_ = true;
    thread_  = std::thread([this] {
        std::vector<SoftFork> result;
        try {
            RpcClient rc(cfg_, auth_);
            auto      dep = rc.call("getdeploymentinfo")["result"]["deployments"];
            if (dep.is_object()) {
                for (const auto& [name, val] : dep.items()) {
                    SoftFork f;
                    f.name   = name;
                    f.type   = val.value("type", std::string{});
                    f.active = val.value("active", false);
                    f.height = val.value("height", int64_t{-1});
                    if (val.contains("bip9") && val["bip9"].is_object()) {
                        const auto& b9        = val["bip9"];
                        f.bip9_status         = b9.value("status", std::string{});
                        f.bip9_since          = b9.value("since", int64_t{0});
                        f.bip9_start_time     = b9.value("start_time", int64_t{0});
                        f.bip9_timeout        = b9.value("timeout", int64_t{0});
                        f.bip9_min_activation = b9.value("min_activation_height", int64_t{0});
                        if (b9.contains("statistics") && b9["statistics"].is_object()) {
                            const auto& st   = b9["statistics"];
                            f.bip9_elapsed   = st.value("elapsed", int64_t{0});
                            f.bip9_count     = st.value("count", int64_t{0});
                            f.bip9_period    = st.value("period", int64_t{0});
                            f.bip9_threshold = st.value("threshold", int64_t{0});
                        }
                    }
                    result.push_back(std::move(f));
                }
                std::sort(result.begin(), result.end(), [](const SoftFork& a, const SoftFork& b) {
                    if (a.active != b.active)
                        return a.active > b.active;
                    return a.name < b.name;
                });
            }
        } catch (...) { // NOLINT(bugprone-empty-catch)
        }
        if (!running_.load())
            return;
        softforks_ = std::move(result);
        loading_   = false;
        loaded_    = true;
        screen_.PostEvent(Event::Custom);
    });
}

Element NetworkTab::render(const AppState& snap) {
    if (!loaded_.load() && !loading_.load())
        fetch();
    std::vector<SoftFork> forks_snap;
    forks_snap = softforks_.get();
    return render_network(snap, forks_snap, loading_.load());
}

void NetworkTab::join() {
    if (thread_.joinable())
        thread_.join();
}
