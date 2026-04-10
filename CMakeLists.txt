cmake_minimum_required(VERSION 3.14)
project(bitcoin-tui VERSION 0.8.0 LANGUAGES C CXX)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# FTXUI
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG        v5.0.0
)
set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(FTXUI_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

# Abseil (required by RE2)
FetchContent_Declare(
  abseil-cpp
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp
  GIT_TAG        20250814.2
)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(abseil-cpp)

# RE2
FetchContent_Declare(
  re2
  GIT_REPOSITORY https://github.com/google/re2
  GIT_TAG        2025-11-05
)
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RE2_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(re2)

# Lua 5.4
FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua
  GIT_TAG        v5.4.8
)
FetchContent_MakeAvailable(lua)

# Build Lua as a static library (no CMakeLists.txt in upstream)
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(REMOVE_ITEM LUA_SOURCES
  ${lua_SOURCE_DIR}/lua.c     # standalone interpreter
  ${lua_SOURCE_DIR}/luac.c    # standalone compiler
  ${lua_SOURCE_DIR}/onelua.c  # single-file build
)
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
if(NOT WIN32)
  target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
endif()

# Sol2
FetchContent_Declare(
  sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2
  GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(sol2)

# CLI11
FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11
  GIT_TAG        v2.6.2
)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cli11)

add_executable(bitcoin-tui
  src/main.cpp
  src/render.cpp
  src/poll.cpp
  src/search.cpp
  src/rpc_client.cpp
  src/bitcoind.cpp
  src/tabs/dashboard.cpp
  src/tabs/network.cpp
  src/tabs/mempool.cpp
  src/tabs/peers.cpp
  src/tabs/tools.cpp
  src/luatable.cpp
  src/tabs/luatab.cpp
)

# src/ is on the include path so rpc_client.hpp can find json.hpp
target_include_directories(bitcoin-tui PRIVATE src/)

target_compile_definitions(bitcoin-tui PRIVATE
  BITCOIN_TUI_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(bitcoin-tui PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  re2::re2
  lua_static
  sol2
  CLI11::CLI11
)

if(WIN32)
  target_link_libraries(bitcoin-tui PRIVATE ws2_32)
endif()

install(TARGETS bitcoin-tui RUNTIME DESTINATION bin COMPONENT runtime)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(bitcoin-tui PRIVATE -Wthread-safety)
endif()

# Catch2
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2
  GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

add_executable(test_json tests/test_json.cpp)
target_include_directories(test_json PRIVATE src/)
target_link_libraries(test_json PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_json COMMAND test_json)

add_executable(test_rpc_config tests/test_rpc_config.cpp src/rpc_client.cpp)
target_include_directories(test_rpc_config PRIVATE src/)
target_link_libraries(test_rpc_config PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_rpc_config COMMAND test_rpc_config)

add_executable(test_bitcoind tests/test_bitcoind.cpp src/bitcoind.cpp)
target_include_directories(test_bitcoind PRIVATE src/)
target_link_libraries(test_bitcoind PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_bitcoind COMMAND test_bitcoind)

add_executable(test_format tests/test_format.cpp)
target_include_directories(test_format PRIVATE src/)
target_link_libraries(test_format PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_format COMMAND test_format)

add_executable(test_state tests/test_state.cpp)
target_include_directories(test_state PRIVATE src/)
target_link_libraries(test_state PRIVATE Catch2::Catch2WithMain)
add_test(NAME test_state COMMAND test_state)

find_package(Threads REQUIRED)
add_executable(test_guarded tests/test_guarded.cpp)
target_include_directories(test_guarded PRIVATE src/)
target_link_libraries(test_guarded PRIVATE Catch2::Catch2WithMain Threads::Threads)
add_test(NAME test_guarded COMMAND test_guarded)

if(WIN32)
  target_link_libraries(test_rpc_config PRIVATE ws2_32)
  target_link_libraries(test_bitcoind PRIVATE ws2_32)
endif()

add_executable(test_cli tests/test_cli.cpp)
target_link_libraries(test_cli PRIVATE Catch2::Catch2WithMain)
add_dependencies(test_cli bitcoin-tui)
add_test(NAME test_cli COMMAND test_cli)
set_tests_properties(test_cli PROPERTIES
    ENVIRONMENT "BITCOIN_TUI_BINARY=$<TARGET_FILE:bitcoin-tui>"
)
