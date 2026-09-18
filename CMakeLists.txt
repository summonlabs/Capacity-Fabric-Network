cmake_minimum_required(VERSION 3.20)

project(CapacityFabricNetwork
        VERSION 1.0.0
        DESCRIPTION "Vendor-neutral C++20 runtime for predictive, generation-bound usable network-capacity modeling"
        HOMEPAGE_URL "https://github.com/summonlabs/Capacity-Fabric-Network"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(CFN_BUILD_TESTS "Build the Capacity Fabric Network test suite" ON)
option(CFN_BUILD_TOOLS "Build the Capacity Fabric Network command line tools" ON)
option(CFN_BUILD_BENCH "Build the Capacity Fabric Network benchmark suite" ON)
option(CFN_BUILD_EXAMPLES "Build the Capacity Fabric Network examples" ON)
option(CFN_ENABLE_ASAN "Build with AddressSanitizer where the toolchain supports it" OFF)
option(CFN_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" ON)
option(CFN_NATIVE_ARCH "Tune code generation for the building machine" OFF)

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Warning / hardening settings, applied per first-party target. A function is
# used rather than an interface library so the installed package exports only
# the real library target.
# ---------------------------------------------------------------------------
# AddressSanitizer on MSVC needs its runtime DLL next to every executable.
# Staging it here means the sanitizer suite runs from a plain shell, not only
# from a developer prompt that happens to have the toolset on PATH.
set(CFN_ASAN_RUNTIME "" CACHE INTERNAL "AddressSanitizer runtime staged next to executables")
if(MSVC AND CFN_ENABLE_ASAN)
  set(_cfn_asan_candidates "")
  if(IS_ABSOLUTE "${CMAKE_CXX_COMPILER}")
    get_filename_component(_cfn_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB _cfn_asan_candidates "${_cfn_compiler_dir}/clang_rt.asan_dynamic-*.dll")
  endif()
  if(NOT _cfn_asan_candidates)
    file(GLOB _cfn_asan_candidates
      "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-*.dll"
      "$ENV{ProgramFiles(x86)}/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-*.dll")
  endif()
  list(FILTER _cfn_asan_candidates EXCLUDE REGEX "dbg")
  if(_cfn_asan_candidates)
    list(GET _cfn_asan_candidates 0 CFN_ASAN_RUNTIME)
    message(STATUS "AddressSanitizer runtime: ${CFN_ASAN_RUNTIME}")
  else()
    message(WARNING "AddressSanitizer runtime was not found; the sanitizer build will "
                    "need the toolset on PATH in order to run")
  endif()
endif()

function(cfn_apply_build_settings target)
  if(MSVC AND CFN_ENABLE_ASAN AND CFN_ASAN_RUNTIME)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CFN_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:${target}>"
      COMMENT "Staging the AddressSanitizer runtime for ${target}")
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:externConstexpr /Zc:inline /utf-8 /EHsc /bigobj)
    if(CFN_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
    target_compile_definitions(${target} PRIVATE
      NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0602 _CRT_SECURE_NO_WARNINGS)
    if(CFN_ENABLE_ASAN)
      target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
      target_link_options(${target} PRIVATE /INCREMENTAL:NO)
      target_compile_definitions(${target} PRIVATE CFN_ASAN_ENABLED=1)
    endif()
    if(CFN_NATIVE_ARCH)
      target_compile_options(${target} PRIVATE /arch:AVX2)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-qual
      -Wformat=2 -Wundef -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual
      -Wdouble-promotion -Wimplicit-fallthrough -Werror=return-type)
    if(CFN_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(CFN_ENABLE_ASAN)
      target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
      target_link_options(${target} PRIVATE -fsanitize=address)
      target_compile_definitions(${target} PRIVATE CFN_ASAN_ENABLED=1)
    endif()
    if(CFN_NATIVE_ARCH)
      target_compile_options(${target} PRIVATE -march=native)
    endif()
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(CFN_PUBLIC_HEADERS
  include/cfn/version.hpp
  include/cfn/cfn.hpp
  include/cfn/core/error.hpp
  include/cfn/core/outcome.hpp
  include/cfn/core/checked.hpp
  include/cfn/core/bounded.hpp
  include/cfn/core/identity.hpp
  include/cfn/core/time.hpp
  include/cfn/core/provenance.hpp
  include/cfn/core/limits.hpp
  include/cfn/core/hash.hpp
  include/cfn/core/random.hpp
  include/cfn/core/text.hpp
  include/cfn/model/resource.hpp
  include/cfn/model/topology.hpp
  include/cfn/model/reservation.hpp
  include/cfn/model/degradation.hpp
  include/cfn/model/demand_shape.hpp
  include/cfn/model/policy.hpp
  include/cfn/model/capacity_model.hpp
  include/cfn/engine/accounting.hpp
  include/cfn/engine/flow.hpp
  include/cfn/engine/fragmentation.hpp
  include/cfn/engine/snapshot.hpp
  include/cfn/engine/invalidation.hpp
  include/cfn/engine/evaluator.hpp
  include/cfn/engine/prediction.hpp
  include/cfn/explain/explanation.hpp
  include/cfn/persist/crc32c.hpp
  include/cfn/persist/journal.hpp
  include/cfn/persist/serialize.hpp
  include/cfn/persist/store.hpp
  include/cfn/fabric/registry.hpp
  include/cfn/fabric/fabric.hpp
  include/cfn/net/framing.hpp
  include/cfn/net/socket.hpp
  include/cfn/net/evidence.hpp
  include/cfn/text/json.hpp
  include/cfn/text/scenario.hpp
  include/cfn/bench/harness.hpp
)

set(CFN_SOURCES
  src/version.cpp
  src/core/error.cpp
  src/core/identity.cpp
  src/core/time.cpp
  src/core/provenance.cpp
  src/core/limits.cpp
  src/core/text.cpp
  src/core/random.cpp
  src/model/resource.cpp
  src/model/topology.cpp
  src/model/reservation.cpp
  src/model/degradation.cpp
  src/model/demand_shape.cpp
  src/model/policy.cpp
  src/model/capacity_model.cpp
  src/engine/accounting.cpp
  src/engine/flow.cpp
  src/engine/fragmentation.cpp
  src/engine/snapshot.cpp
  src/engine/invalidation.cpp
  src/engine/evaluator.cpp
  src/engine/prediction.cpp
  src/explain/explanation.cpp
  src/persist/crc32c.cpp
  src/persist/journal.cpp
  src/persist/serialize.cpp
  src/persist/store.cpp
  src/fabric/registry.cpp
  src/fabric/fabric.cpp
  src/net/framing.cpp
  src/net/socket.cpp
  src/net/evidence.cpp
  src/text/json.cpp
  src/text/scenario.cpp
  src/bench/harness.cpp
  src/bench/synthetic.cpp
)

add_library(cfn ${CFN_SOURCES})
add_library(cfn::cfn ALIAS cfn)
add_library(CapacityFabricNetwork::cfn ALIAS cfn)

target_include_directories(cfn
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

cfn_apply_build_settings(cfn)
target_compile_features(cfn PUBLIC cxx_std_20)

set_target_properties(cfn PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  OUTPUT_NAME cfn
  DEBUG_POSTFIX "d")

if(WIN32)
  target_link_libraries(cfn PRIVATE ws2_32)
endif()

if(BUILD_SHARED_LIBS)
  target_compile_definitions(cfn PRIVATE CFN_SHARED=1)
  generate_export_header(cfn
    BASE_NAME CFN
    EXPORT_MACRO_NAME CFN_API
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/cfn/export.hpp")
  target_include_directories(cfn PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/cfn/export.hpp"
          DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cfn")
endif()

# ---------------------------------------------------------------------------
# Tests / tools / benchmarks / examples
# ---------------------------------------------------------------------------
if(CFN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CFN_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(CFN_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(CFN_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
install(TARGETS cfn
        EXPORT cfn-targets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(DIRECTORY include/cfn DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT cfn-targets
        FILE cfn-targets.cmake
        NAMESPACE cfn::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cfn")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cfn-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cfn-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cfn")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cfn-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/cfn-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cfn-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cfn")

install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/cfn" RENAME LICENSE.txt)

message(STATUS "Capacity Fabric Network ${PROJECT_VERSION} | build type: ${CMAKE_BUILD_TYPE} | ASan: ${CFN_ENABLE_ASAN} | Werror: ${CFN_WARNINGS_AS_ERRORS}")