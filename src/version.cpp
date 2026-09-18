// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/version.hpp"

#include <string>

namespace cfn {

std::string build_banner() {
  std::string banner = "Capacity Fabric Network ";
  banner.append(version_string);
  banner.append(" (format ");
  banner.append(std::to_string(format_version));
  banner.append(")");
#if defined(_MSC_VER)
  banner.append(" msvc=");
  banner.append(std::to_string(_MSC_VER));
#elif defined(__clang__)
  banner.append(" clang=");
  banner.append(__clang_version__);
#elif defined(__GNUC__)
  banner.append(" gcc=");
  banner.append(std::to_string(__GNUC__));
#endif
#if defined(NDEBUG)
  banner.append(" build=release");
#else
  banner.append(" build=debug");
#endif
#if defined(CFN_ASAN_ENABLED)
  banner.append(" asan=on");
#endif
  return banner;
}

}  // namespace cfn
