#ifndef _WIN32

#include <catch2/catch_test_macros.hpp>

#include "bitcoind.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

// RAII helper: creates a temp dir, writes an executable file into it,
// and sets PATH to include (or not include) that dir for the duration of the test.
struct TempDir {
    fs::path path;

    TempDir() {
        path = fs::temp_directory_path() / ("btui_test_" + std::to_string(getpid()));
        fs::create_directories(path);
    }

    // Write an empty executable file named `name` inside the dir.
    void make_executable(const std::string& name) const {
        fs::path p = path / name;
        std::ofstream(p).close();
        chmod(p.c_str(), 0755);
    }

    ~TempDir() { fs::remove_all(path); }
};

struct PathGuard {
    std::string saved;

    explicit PathGuard(const std::string& new_path) {
        const char* p = getenv("PATH");
        saved          = p ? p : "";
        setenv("PATH", new_path.c_str(), 1);
    }

    ~PathGuard() { setenv("PATH", saved.c_str(), 1); }
};

// ============================================================================

TEST_CASE("find_bitcoind: empty PATH returns empty string") {
    PathGuard g("");
    CHECK(find_bitcoind().empty());
}

TEST_CASE("find_bitcoind: PATH with no bitcoind returns empty string") {
    TempDir   dir;
    PathGuard g(dir.path.string());
    CHECK(find_bitcoind().empty());
}

TEST_CASE("find_bitcoind: finds bitcoind in single PATH entry") {
    TempDir dir;
    dir.make_executable("bitcoind");
    PathGuard g(dir.path.string());

    auto result = find_bitcoind();
    CHECK(result == (dir.path / "bitcoind").string());
}

TEST_CASE("find_bitcoind: finds bitcoind in second PATH entry") {
    TempDir first, second;
    second.make_executable("bitcoind");
    PathGuard g(first.path.string() + ":" + second.path.string());

    auto result = find_bitcoind();
    CHECK(result == (second.path / "bitcoind").string());
}

TEST_CASE("find_bitcoind: prefers first match in PATH") {
    TempDir a, b;
    a.make_executable("bitcoind");
    b.make_executable("bitcoind");
    PathGuard g(a.path.string() + ":" + b.path.string());

    auto result = find_bitcoind();
    CHECK(result == (a.path / "bitcoind").string());
}

TEST_CASE("find_bitcoind: non-executable file is not returned") {
    TempDir   dir;
    fs::path  p = dir.path / "bitcoind";
    std::ofstream(p).close();
    chmod(p.string().c_str(), 0644);  // readable but not executable
    PathGuard g(dir.path.string());

    CHECK(find_bitcoind().empty());
}

#endif // _WIN32
