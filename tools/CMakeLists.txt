add_executable(cfn-cli cfn-cli.cpp)
target_link_libraries(cfn-cli PRIVATE cfn::cfn)
cfn_apply_build_settings(cfn-cli)

add_executable(cfn-evidence-node cfn-evidence-node.cpp)
target_link_libraries(cfn-evidence-node PRIVATE cfn::cfn)
cfn_apply_build_settings(cfn-evidence-node)

add_executable(cfn-store-probe cfn-store-probe.cpp)
target_link_libraries(cfn-store-probe PRIVATE cfn::cfn)
cfn_apply_build_settings(cfn-store-probe)

install(TARGETS cfn-cli cfn-evidence-node cfn-store-probe
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")