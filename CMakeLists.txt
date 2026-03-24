cmake_minimum_required(VERSION 3.14)
project(bitcoin-tui VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# FTXUI
if (DEFINED ENV{FTXUI_SRC})
  FetchContent_Declare(
    ftxui
    SOURCE_DIR "$ENV{FTXUI_SRC}"
  )
else()
  FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG        v5.0.0
  )
endif()
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

# Catch2
if (DEFINED ENV{CATCH2_SRC})
  FetchContent_Declare(
    Catch2
    SOURCE_DIR "$ENV{CATCH2_SRC}"
  )
else()
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2
    GIT_TAG        v3.7.1
  )
endif()
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
