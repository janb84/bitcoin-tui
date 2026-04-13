#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <csignal>
#  include <thread>
#  include <unistd.h>
#  include <sys/wait.h>
#endif

// Path to the bitcoin-tui binary, injected via CMake ENVIRONMENT.
static std::string binary() {
    const char* p = std::getenv("BITCOIN_TUI_BINARY");
    REQUIRE(p != nullptr);
#ifdef _WIN32
    // Quote the path so spaces in it don't confuse cmd.exe.
    return "\"" + std::string(p) + "\"";
#else
    return p;
#endif
}

// Redirect that discards all output on both Unix and Windows.
static const char* null_sink() {
#ifdef _WIN32
    return " >NUL 2>&1";
#else
    return " >/dev/null 2>&1";
#endif
}

static int exit_code(const std::string& cmd) {
    int status = std::system(cmd.c_str());
#ifdef _WIN32
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

#ifndef _WIN32
static constexpr int kTimedOut = -2;

static int exit_code_with_timeout(const std::string& cmd, std::chrono::milliseconds timeout) {
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    int  status   = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        REQUIRE(waited >= 0);
        if (waited == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return kTimedOut;
}

static std::string shell_quote(const std::string& s) {
    std::string out{"'"};
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

static std::filesystem::path make_temp_dir() {
    auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    auto p = std::filesystem::temp_directory_path() /
             ("bitcoin-tui-cli-test-" + std::to_string(static_cast<long long>(ts)) + "-" +
              std::to_string(std::rand()));
    std::filesystem::create_directories(p);
    return p;
}

struct TempDir {
    std::filesystem::path path = make_temp_dir();
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

static std::filesystem::path default_cfg_dir_for_home(const std::filesystem::path& home) {
#ifdef __APPLE__
    return home / "Library" / "Application Support" / "bitcoin-tui";
#else
    return home / ".config" / "bitcoin-tui";
#endif
}

static int exit_code_with_home(const std::filesystem::path& home, const std::string& args) {
    // Override XDG_CONFIG_HOME to match default_cfg_dir_for_home(), so Linux
    // CI environments with a pre-set XDG_CONFIG_HOME don't redirect the binary
    // to a different config location and cause the TUI to start unexpectedly.
    return exit_code("HOME=" + shell_quote(home.string()) +
                     " XDG_CONFIG_HOME=" + shell_quote((home / ".config").string()) +
                     " " + binary() + " " + args + " >/dev/null 2>&1");
}
#endif

// ---------------------------------------------------------------------------
// --help / --version
// Regression: these used to return 0 from configure() while the caller only
// skipped run() on non-zero, so the TUI would launch instead of exiting.
// ---------------------------------------------------------------------------

TEST_CASE("--help exits 0 without launching TUI") {
    CHECK(exit_code(binary() + " --help" + null_sink()) == 0);
}

TEST_CASE("--version exits 0 without launching TUI") {
    CHECK(exit_code(binary() + " --version" + null_sink()) == 0);
}

TEST_CASE("-v exits 0 without launching TUI") {
    CHECK(exit_code(binary() + " -v" + null_sink()) == 0);
}

// ---------------------------------------------------------------------------
// Unknown / invalid options
// ---------------------------------------------------------------------------

TEST_CASE("unknown option exits non-zero") {
    CHECK(exit_code(binary() + " --does-not-exist" + null_sink()) != 0);
}

// ---------------------------------------------------------------------------
// --port validation
// ---------------------------------------------------------------------------

TEST_CASE("--port accepts valid port with --version") {
    CHECK(exit_code(binary() + " --port 8332 --version" + null_sink()) == 0);
    CHECK(exit_code(binary() + " --port 1 --version" + null_sink()) == 0);
    CHECK(exit_code(binary() + " --port 65535 --version" + null_sink()) == 0);
}

TEST_CASE("--port rejects out-of-range values") {
    CHECK(exit_code(binary() + " --port 0" + null_sink()) != 0);
    CHECK(exit_code(binary() + " --port 65536" + null_sink()) != 0);
}

TEST_CASE("--port rejects non-numeric value") {
    CHECK(exit_code(binary() + " --port abc" + null_sink()) != 0);
}

// ---------------------------------------------------------------------------
// Network flags
// ---------------------------------------------------------------------------

TEST_CASE("--testnet and --regtest together exit non-zero") {
    CHECK(exit_code(binary() + " --testnet --regtest" + null_sink()) != 0);
}

TEST_CASE("--testnet and --signet together exit non-zero") {
    CHECK(exit_code(binary() + " --testnet --signet" + null_sink()) != 0);
}

TEST_CASE("--regtest and --signet together exit non-zero") {
    CHECK(exit_code(binary() + " --regtest --signet" + null_sink()) != 0);
}

// ---------------------------------------------------------------------------
// Lua flags — parse-time only (no running node required)
// --version short-circuits before any connection attempt, so combining it
// with Lua flags verifies the flags are accepted by the parser.
// ---------------------------------------------------------------------------

TEST_CASE("--tab with --version exits 0") {
    CHECK(exit_code(binary() + " --tab /nonexistent.lua --version" + null_sink()) == 0);
}

TEST_CASE("--allow-rpc with --version exits 0") {
    CHECK(exit_code(binary() + " --allow-rpc getblockchaininfo --version" + null_sink()) == 0);
}

#ifndef _WIN32
TEST_CASE("missing default config path does not fail") {
    TempDir home;
    CHECK(exit_code_with_home(home.path, "--version") == 0);
}

TEST_CASE("present default config is auto-loaded") {
    TempDir home;
    auto    cfg_dir  = default_cfg_dir_for_home(home.path);
    auto    cfg_path = cfg_dir / "config.toml";
    std::filesystem::create_directories(cfg_dir);

    // If the default config is loaded, the invalid integer causes a CLI11
    // parse error and the binary exits non-zero before the TUI starts.
    std::ofstream(cfg_path) << "refresh = \"not-a-number\"\n";

    int rc = exit_code_with_timeout(
        "HOME=" + shell_quote(home.path.string()) +
            " XDG_CONFIG_HOME=" + shell_quote((home.path / ".config").string()) +
            " " + binary() + " >/dev/null 2>&1",
        std::chrono::seconds(2));

    CHECK(rc != kTimedOut);
    CHECK(rc != 0);
}
#endif
