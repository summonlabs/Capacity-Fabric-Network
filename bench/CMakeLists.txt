add_executable(cfn-bench bench_main.cpp)
target_link_libraries(cfn-bench PRIVATE cfn::cfn)
cfn_apply_build_settings(cfn-bench)