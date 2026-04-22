find_package(Threads REQUIRED)

# All unit tests share one binary — faster link, less boilerplate.
# test_cli stays separate: it is a process-level black-box test that spawns
# the bitcoin-tui binary, so it cannot be merged.
add_executable(bitcoin-tui-tests
  test_json.cpp
  test_format.cpp
  test_state.cpp
  test_guarded.cpp
  test_rpc_config.cpp
  test_bitcoind.cpp
  test_footer_spec.cpp
)
target_include_directories(bitcoin-tui-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/)
target_link_libraries(bitcoin-tui-tests PRIVATE
  rpc_client_obj
  bitcoind_obj
  Catch2::Catch2WithMain
  Threads::Threads
)
add_test(NAME bitcoin-tui-tests COMMAND bitcoin-tui-tests)

add_executable(test_cli test_cli.cpp)
target_link_libraries(test_cli PRIVATE Catch2::Catch2WithMain)
add_dependencies(test_cli bitcoin-tui)
add_test(NAME test_cli COMMAND test_cli)
set_tests_properties(test_cli PROPERTIES
    ENVIRONMENT "BITCOIN_TUI_BINARY=$<TARGET_FILE:bitcoin-tui>"
)
