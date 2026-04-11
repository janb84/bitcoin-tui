#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string fmt_int(int64_t n) {
    bool        negative = n < 0;
    std::string s        = std::to_string(std::abs(n));
    int         pos      = static_cast<int>(s.size()) - 3;
    while (pos > 0) {
        s.insert(static_cast<size_t>(pos), ",");
        pos -= 3;
    }
    return negative ? "-" + s : s;
}

inline std::string fmt_height(int64_t n) {
    std::string s   = std::to_string(n);
    int         pos = static_cast<int>(s.size()) - 3;
    while (pos > 0) {
        s.insert(static_cast<size_t>(pos), "'");
        pos -= 3;
    }
    return s;
}

inline std::string fmt_bytes(int64_t b) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    if (b >= 1'000'000'000LL)
        ss << static_cast<double>(b) / 1e9 << " GB";
    else if (b >= 1'000'000LL)
        ss << static_cast<double>(b) / 1e6 << " MB";
    else if (b >= 1'000LL)
        ss << static_cast<double>(b) / 1e3 << " KB";
    else
        ss << b << " B";
    return ss.str();
}

inline std::string fmt_difficulty(double d) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (d >= 1e18)
        ss << d / 1e18 << " E";
    else if (d >= 1e15)
        ss << d / 1e15 << " P";
    else if (d >= 1e12)
        ss << d / 1e12 << " T";
    else if (d >= 1e9)
        ss << d / 1e9 << " G";
    else
        ss << d;
    return ss.str();
}

inline std::string fmt_hashrate(double h) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (h >= 1e21)
        ss << h / 1e21 << " ZH/s";
    else if (h >= 1e18)
        ss << h / 1e18 << " EH/s";
    else if (h >= 1e15)
        ss << h / 1e15 << " PH/s";
    else if (h >= 1e12)
        ss << h / 1e12 << " TH/s";
    else if (h >= 1e9)
        ss << h / 1e9 << " GH/s";
    else if (h >= 1e6)
        ss << h / 1e6 << " MH/s";
    else if (h >= 1e3)
        ss << h / 1e3 << " kH/s";
    else
        ss << h << " H/s";
    return ss.str();
}

inline std::string fmt_satsvb(double btc_per_kvb) {
    double             sats_per_vb = btc_per_kvb * 1e5; // BTC/kvB → sat/vB
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << sats_per_vb << " sat/vB";
    return ss.str();
}

template <typename T> std::chrono::system_clock::time_point to_time_point(T epoch_secs) {
    return std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<T>{epoch_secs})};
}

enum class TimeFmt { YMDHMS, YMD, HMS, HMSM };

inline std::string fmt_localtime(std::chrono::system_clock::time_point tp, TimeFmt fmt) {
    auto    t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    switch (fmt) {
    case TimeFmt::YMDHMS:
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;
    case TimeFmt::YMD:
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        break;
    case TimeFmt::HMS:
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;
    case TimeFmt::HMSM: {
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                 static_cast<int>(ms.count()));
        break;
    }
    }
    return buf;
}

inline std::string now_string() {
    return fmt_localtime(std::chrono::system_clock::now(), TimeFmt::HMS);
}

inline std::string fmt_btc(double btc, int precision = 8) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << btc << " BTC";
    return ss.str();
}

inline std::string fmt_age(int64_t secs) {
    if (secs < 60)
        return std::to_string(secs) + "s";
    if (secs < 3600)
        return std::to_string(secs / 60) + "m " + std::to_string(secs % 60) + "s";
    return std::to_string(secs / 3600) + "h " + std::to_string((secs % 3600) / 60) + "m";
}

inline std::string fmt_time_ago(int64_t timestamp) {
    int64_t now_secs = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    int64_t diff = now_secs - timestamp;
    if (diff < 0)
        return "just now";
    if (diff < 60)
        return std::to_string(diff) + "s ago";
    if (diff < 3600)
        return std::to_string(diff / 60) + "m ago";
    if (diff < 86400)
        return std::to_string(diff / 3600) + "h ago";
    return std::to_string(diff / 86400) + "d ago";
}

inline std::string trimmed(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

inline std::string extract_miner(const std::string& hex) {
    std::string best, run;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int b = std::stoi(hex.substr(i, 2), nullptr, 16);
        if (b >= 0x20 && b < 0x7f && b != '/') {
            run += static_cast<char>(b);
        } else {
            if (run.size() >= 4 && run.size() > best.size())
                best = run;
            run.clear();
        }
    }
    if (run.size() >= 4 && run.size() > best.size())
        best = run;
    if (best.size() > 24)
        best = best.substr(0, 24);
    return best.empty() ? "—" : best;
}
