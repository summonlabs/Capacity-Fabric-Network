add_executable(cfn-quickstart quickstart.cpp)
target_link_libraries(cfn-quickstart PRIVATE cfn::cfn)
cfn_apply_build_settings(cfn-quickstart)