#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

// Host-side formatting helpers. Value formatting for tab content (numbers, sizes,
// fee rates, ages, …) lives in the Lua tabs that render it. Only what C++ itself
// still prints belongs here.

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

inline std::string trimmed(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}
