#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <sys/wait.h>

// Path to the bitcoin-tui binary, injected via CMake ENVIRONMENT.
static std::string binary() {
    const char* p = std::getenv("BITCOIN_TUI_BINARY");
    REQUIRE(p != nullptr);
    return p;
}

static int exit_code(const std::string& cmd) {
    int status = std::system(cmd.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Regression: --help and --version used to return 0 from configure() while the
// caller only skipped run() on non-zero, so the TUI would launch instead of
// exiting cleanly.

TEST_CASE("--help exits 0 without launching TUI") {
    CHECK(exit_code(binary() + " --help >/dev/null 2>&1") == 0);
}

TEST_CASE("--version exits 0 without launching TUI") {
    CHECK(exit_code(binary() + " --version >/dev/null 2>&1") == 0);
}

TEST_CASE("-v exits 0 without launching TUI") {
    CHECK(exit_code(binary() + " -v >/dev/null 2>&1") == 0);
}
