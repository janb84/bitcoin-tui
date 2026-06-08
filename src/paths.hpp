#pragma once

// Shared filesystem-path resolution for bitcoin-tui.
//
// All config-directory logic lives here so the startup reader (main.cpp) and the
// Settings-tab writer (tabs/luatab.cpp) can never disagree about where
// config.toml lives. It also handles two Linux footguns:
//   - running under sudo: HOME becomes /root and XDG_CONFIG_HOME is cleared by
//     env_reset, so config would land in root's home. We resolve paths for the
//     invoking user (SUDO_USER) instead, and chown anything we create back to
//     them so the files stay usable without sudo.
//   - locating bundled lua/ scripts: argv[0] is unreliable (bare name when
//     launched from PATH), so we read the real executable path from the OS.

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace paths {

// Pure decision used by user_home(): when running as root via sudo and the
// invoking user's home is known, prefer it over root's $HOME. Extracted so it
// is unit-testable without actually being root.
inline std::string choose_home(bool running_as_root, const std::string& sudo_user_home,
                               const std::string& home_env) {
    if (running_as_root && !sudo_user_home.empty())
        return sudo_user_home;
    return home_env;
}

// Home directory of the user the app acts on behalf of. When run as root via
// sudo, prefer the invoking user's home (SUDO_USER) over root's.
inline std::string user_home() {
    bool        running_as_root = false;
    std::string sudo_user_home;
#ifndef _WIN32
    running_as_root = (::geteuid() == 0);
    if (running_as_root) {
        const char* sudo_user = std::getenv("SUDO_USER");
        if (sudo_user && sudo_user[0] != '\0') {
            if (const struct passwd* pw = ::getpwnam(sudo_user))
                if (pw->pw_dir && pw->pw_dir[0] != '\0')
                    sudo_user_home = pw->pw_dir;
        }
    }
#endif
    const char* home = std::getenv("HOME");
    return choose_home(running_as_root, sudo_user_home, home ? home : "");
}

inline std::string& config_file_override() {
    static std::string value;
    return value;
}

// Absolute path to the bitcoin-tui config directory. Resolved purely from the
// environment. Returns "" when no home/APPDATA can be determined.
inline std::string config_dir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
        return std::string(appdata) + "\\bitcoin-tui";
    return "";
#elif defined(__APPLE__)
    std::string home = user_home();
    return home.empty() ? "" : home + "/Library/Application Support/bitcoin-tui";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
        if (xdg[0] != '\0')
            return std::string(xdg) + "/bitcoin-tui";
    std::string home = user_home();
    return home.empty() ? "" : home + "/.config/bitcoin-tui";
#endif
}

// Absolute path to config.toml. Honors config_file_override() first; otherwise
// it is config_dir()/config.toml. Returns "" when neither an override nor a
// resolvable config_dir() is available — callers treat that as a hard error
// rather than guessing a location.
inline std::string config_file() {
    if (!config_file_override().empty())
        return config_file_override();
    std::string dir = config_dir();
    if (dir.empty())
        return "";
    return (std::filesystem::path(dir) / "config.toml").string();
}

// Absolute path to the running executable, resolved from the OS rather than
// argv[0] (which is just the bare name when launched from PATH). Returns "" on
// failure; callers should fall back to argv[0].
inline std::string executable_path() {
#if defined(_WIN32)
    char  buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::string(buf, n);
#elif defined(__APPLE__)
    char     buf[4096];
    uint32_t size = sizeof(buf);
    if (::_NSGetExecutablePath(buf, &size) == 0)
        return buf;
    return "";
#else
    std::error_code ec;
    auto            p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : p.string();
#endif
}

// When running as root via sudo, chown a path we just created back to the
// invoking user so it remains owned/usable without sudo. No-op otherwise.
inline void chown_to_invoking_user(const std::string& path) {
#ifndef _WIN32
    if (::geteuid() != 0)
        return;
    const char* uid_s = std::getenv("SUDO_UID");
    const char* gid_s = std::getenv("SUDO_GID");
    if (!uid_s || !gid_s)
        return;
    auto uid = static_cast<uid_t>(std::strtoul(uid_s, nullptr, 10));
    auto gid = static_cast<gid_t>(std::strtoul(gid_s, nullptr, 10));
    if (::chown(path.c_str(), uid, gid) != 0)
        return; // best-effort; nothing actionable on failure
#else
    (void)path;
#endif
}

} // namespace paths
