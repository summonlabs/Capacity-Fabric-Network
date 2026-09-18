# Capacity Fabric Network - independent downstream consumer.
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs.
#
# This project is deliberately not part of the Capacity Fabric Network build.
# It exists to prove the installed package works for a downstream project that
# only knows about find_package: no source tree, no exported build directory.
cmake_minimum_required(VERSION 3.20)

project(cfn_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(cfn 1.0 CONFIG REQUIRED)

add_executable(cfn-consumer main.cpp)
target_link_libraries(cfn-consumer PRIVATE cfn::cfn)

if(MSVC)
  target_compile_options(cfn-consumer PRIVATE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(cfn-consumer PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME cfn-consumer-runs COMMAND cfn-consumer)
