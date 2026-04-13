cmake_minimum_required(VERSION 3.22)
project(bitcoin-tui VERSION 0.9.0 LANGUAGES C CXX)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
  add_compile_options(/EHsc)
endif()

include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# FTXUI
FetchContent_Declare(
  ftxui
  URL      https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v6.1.9.tar.gz
  URL_HASH SHA256=45819c1e54914783d4a1ca5633885035d74146778a1f74e1213cdb7b76340e71
)
set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(FTXUI_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

# Abseil (required by RE2)
FetchContent_Declare(
  abseil-cpp
  URL      https://github.com/abseil/abseil-cpp/archive/refs/tags/20250814.2.tar.gz
  URL_HASH SHA256=f9148fb00ec98a2396bdf875c99a78e6a70afa662b107862d92b285d857a8320
)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(abseil-cpp)

# RE2
FetchContent_Declare(
  re2
  URL      https://github.com/google/re2/archive/refs/tags/2025-11-05.tar.gz
  URL_HASH SHA256=87f6029d2f6de8aa023654240a03ada90e876ce9a4676e258dd01ea4c26ffd67
)
set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RE2_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(re2)

# Lua 5.4
FetchContent_Declare(
  lua
  URL      https://github.com/lua/lua/archive/refs/tags/v5.4.8.tar.gz
  URL_HASH SHA256=d85b70a65f43c5d2254944d58d625e822c8e2e10d9c6a3bd9b5b657e46376a19
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
  URL      https://github.com/ThePhD/sol2/archive/refs/tags/v3.5.0.tar.gz
  URL_HASH SHA256=86c0f6d2836b184a250fc2907091c076bf53c9603dd291eaebade36cc342e13c
)
FetchContent_MakeAvailable(sol2)

# CLI11
FetchContent_Declare(
  cli11
  URL      https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.6.2.tar.gz
  URL_HASH SHA256=c6ea6b2e5608b3ea8617999bd5f47420c71b2ebdb8dc4767c1034d1da5785711
)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cli11)

# Object libraries for sources shared between bitcoin-tui and the test suite.
# Compiled once; CMake 3.12+ propagates object files + PUBLIC includes + PUBLIC
# link libs when you target_link_libraries against an OBJECT library.
add_library(rpc_client_obj OBJECT src/rpc_client.cpp)
target_include_directories(rpc_client_obj PUBLIC src/)
if(WIN32)
  target_link_libraries(rpc_client_obj PUBLIC ws2_32)
endif()

add_library(bitcoind_obj OBJECT src/bitcoind.cpp)
target_include_directories(bitcoind_obj PUBLIC src/)
if(WIN32)
  target_link_libraries(bitcoind_obj PUBLIC ws2_32)
endif()

add_executable(bitcoin-tui
  src/main.cpp
  src/render.cpp
  src/poll.cpp
  src/search.cpp
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

if(MSVC)
  target_compile_definitions(bitcoin-tui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_sources(bitcoin-tui PRIVATE contrib/windeploy/bitcoin-tui.rc)
endif()

target_link_libraries(bitcoin-tui PRIVATE
  rpc_client_obj
  bitcoind_obj
  ftxui::screen
  ftxui::dom
  ftxui::component
  re2::re2
  lua_static
  sol2
  CLI11::CLI11
)

install(TARGETS bitcoin-tui RUNTIME DESTINATION bin COMPONENT runtime)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(bitcoin-tui PRIVATE -Wthread-safety)
endif()

# Catch2
FetchContent_Declare(
  Catch2
  URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
  URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
)
FetchContent_MakeAvailable(Catch2)

enable_testing()
add_subdirectory(tests)
