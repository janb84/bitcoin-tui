#include <cstdlib>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "paths.hpp"

// RAII helper: set an env var for the duration of a scope and restore the
// previous value (or unset) afterwards, so tests don't leak into each other.
namespace {
class ScopedEnv {
  public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* prev = std::getenv(name)) {
            had_prev_ = true;
            prev_     = prev;
        }
        if (value)
            ::setenv(name, value, 1);
        else
            ::unsetenv(name);
    }
    ~ScopedEnv() {
        if (had_prev_)
            ::setenv(name_.c_str(), prev_.c_str(), 1);
        else
            ::unsetenv(name_.c_str());
    }
    ScopedEnv(const ScopedEnv&)            = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

  private:
    std::string name_;
    bool        had_prev_ = false;
    std::string prev_;
};
} // namespace

// ============================================================================
// choose_home — pure sudo-resolution logic
// ============================================================================

TEST_CASE("choose_home — non-root always uses $HOME") {
    CHECK(paths::choose_home(false, "/home/alice", "/home/bob") == "/home/bob");
    CHECK(paths::choose_home(false, "", "/home/bob") == "/home/bob");
}

TEST_CASE("choose_home — root with known SUDO_USER home prefers that user") {
    CHECK(paths::choose_home(true, "/home/alice", "/root") == "/home/alice");
}

TEST_CASE("choose_home — root without a resolvable SUDO_USER falls back to $HOME") {
    CHECK(paths::choose_home(true, "", "/root") == "/root");
}

// ============================================================================
// user_home — env-driven (non-root in the test runner)
// ============================================================================

TEST_CASE("user_home — returns $HOME when not running as root") {
    ScopedEnv home("HOME", "/home/tester");
    CHECK(paths::user_home() == "/home/tester");
}

namespace {
// RAII guard for the process-wide config-file override so tests don't leak it.
class ScopedOverride {
  public:
    explicit ScopedOverride(const std::string& v) : prev_(paths::config_file_override()) {
        paths::config_file_override() = v;
    }
    ~ScopedOverride() { paths::config_file_override() = prev_; }
    ScopedOverride(const ScopedOverride&)            = delete;
    ScopedOverride& operator=(const ScopedOverride&) = delete;

  private:
    std::string prev_;
};
} // namespace

// ============================================================================
// config_file — override + dir-derived path
// ============================================================================

TEST_CASE("config_file — override wins over everything") {
    ScopedEnv      home("HOME", "/home/tester");
    ScopedOverride ov("/explicit/my-config.toml");
    CHECK(paths::config_file() == "/explicit/my-config.toml");
}

TEST_CASE("config_file — derived from config_dir when no override") {
    ScopedOverride ov(""); // ensure no leaked override
    ScopedEnv      home("HOME", "/home/tester");
#if !defined(_WIN32) && !defined(__APPLE__)
    ScopedEnv xdg("XDG_CONFIG_HOME", nullptr);
    CHECK(paths::config_file() == "/home/tester/.config/bitcoin-tui/config.toml");
#elif defined(__APPLE__)
    CHECK(paths::config_file() ==
          "/home/tester/Library/Application Support/bitcoin-tui/config.toml");
#endif
}

TEST_CASE("config_file — empty when no override and no usable HOME") {
    ScopedOverride ov(""); // ensure no leaked override
    ScopedEnv      home("HOME", nullptr);
#if !defined(_WIN32) && !defined(__APPLE__)
    ScopedEnv xdg("XDG_CONFIG_HOME", nullptr);
    CHECK(paths::config_file().empty());
#elif defined(__APPLE__)
    CHECK(paths::config_file().empty());
#else
    ScopedEnv appdata("APPDATA", nullptr);
    CHECK(paths::config_file().empty());
#endif
}

TEST_CASE("config_dir — appends the bitcoin-tui suffix under a home dir") {
    ScopedEnv home("HOME", "/home/tester");
#if !defined(_WIN32) && !defined(__APPLE__)
    ScopedEnv xdg("XDG_CONFIG_HOME", nullptr); // force the HOME fallback
    CHECK(paths::config_dir() == "/home/tester/.config/bitcoin-tui");
#elif defined(__APPLE__)
    CHECK(paths::config_dir() == "/home/tester/Library/Application Support/bitcoin-tui");
#endif
}

#if !defined(_WIN32) && !defined(__APPLE__)
TEST_CASE("config_dir — honors XDG_CONFIG_HOME when set (Linux)") {
    ScopedEnv xdg("XDG_CONFIG_HOME", "/custom/xdg");
    CHECK(paths::config_dir() == "/custom/xdg/bitcoin-tui");
}

TEST_CASE("config_dir — ignores an empty XDG_CONFIG_HOME (Linux)") {
    ScopedEnv home("HOME", "/home/tester");
    ScopedEnv xdg("XDG_CONFIG_HOME", "");
    CHECK(paths::config_dir() == "/home/tester/.config/bitcoin-tui");
}
#endif

// ============================================================================
// executable_path — resolved from the OS, not argv[0]
// ============================================================================

TEST_CASE("executable_path — returns an existing absolute path") {
    std::string exe = paths::executable_path();
    REQUIRE_FALSE(exe.empty());
    std::filesystem::path p(exe);
    CHECK(p.is_absolute());
    CHECK(std::filesystem::exists(p));
}
