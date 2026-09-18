set(CFN_TEST_SOURCES
  test_main.cpp
  test_support.cpp
  test_core.cpp
  test_accounting.cpp
  test_flow.cpp
  test_fragmentation.cpp
  test_snapshot.cpp
  test_invalidation.cpp
  test_prediction.cpp
  test_persistence.cpp
  test_scenario.cpp
  test_explain.cpp
  test_property.cpp
  test_adversarial.cpp
  test_concurrency.cpp
  test_multiprocess.cpp
)

add_executable(cfn-tests ${CFN_TEST_SOURCES})
target_link_libraries(cfn-tests PRIVATE cfn::cfn)
cfn_apply_build_settings(cfn-tests)
target_include_directories(cfn-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(cfn-tests PRIVATE
  CFN_STORE_PROBE_PATH="$<TARGET_FILE:cfn-store-probe>"
  CFN_EVIDENCE_NODE_PATH="$<TARGET_FILE:cfn-evidence-node>"
  CFN_CLI_PATH="$<TARGET_FILE:cfn-cli>")
add_dependencies(cfn-tests cfn-store-probe cfn-evidence-node cfn-cli)

# Each suite runs as its own CTest entry. No timeout is registered: a hanging
# test is a defect to diagnose, never something to mask with a deadline.
set(CFN_TEST_SUITES
  core
  accounting
  flow
  fragmentation
  snapshot
  invalidation
  prediction
  persistence
  scenario
  explain
  property
  adversarial
  concurrency
  multiprocess
)

foreach(suite IN LISTS CFN_TEST_SUITES)
  add_test(NAME cfn-${suite} COMMAND cfn-tests "--filter=${suite}.")
endforeach()

add_test(NAME cfn-list COMMAND cfn-tests --list)