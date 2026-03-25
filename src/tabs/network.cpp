#include "network.hpp"

#include <algorithm>

#include "render.hpp"

using namespace ftxui;

NetworkTab::NetworkTab(RpcConfig cfg, Guarded<RpcAuth>& auth, ScreenInteractive& screen,
                       std::atomic<bool>& running)
    : cfg_(std::move(cfg)), auth_(auth), screen_(screen), running_(running) {}

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
        {
            StdLockGuard lock(mtx_);
            softforks_ = std::move(result);
        }
        loading_ = false;
        loaded_  = true;
        screen_.PostEvent(Event::Custom);
    });
}

Element NetworkTab::render(const AppState& snap) {
    if (!loaded_.load() && !loading_.load())
        fetch();
    std::vector<SoftFork> forks_snap;
    {
        StdLockGuard lock(mtx_);
        forks_snap = softforks_;
    }
    return render_network(snap, forks_snap, loading_.load());
}

void NetworkTab::join() {
    if (thread_.joinable())
        thread_.join();
}
