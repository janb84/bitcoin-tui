cmake_minimum_required(VERSION 3.14)
project(bitcoin-tui VERSION 0.8.0 LANGUAGES CXX)

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
FetchContent_MakeAvailable(ftxui)

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
)

if(WIN32)
  target_link_libraries(bitcoin-tui PRIVATE ws2_32)
endif()

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

add_executable(test_cli tests/test_cli.cpp)
target_link_libraries(test_cli PRIVATE Catch2::Catch2WithMain)
add_dependencies(test_cli bitcoin-tui)
add_test(NAME test_cli COMMAND test_cli)
set_tests_properties(test_cli PROPERTIES
    ENVIRONMENT "BITCOIN_TUI_BINARY=$<TARGET_FILE:bitcoin-tui>"
)
