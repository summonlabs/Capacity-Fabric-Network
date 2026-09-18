// Capacity Fabric Network - version identification.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_VERSION_HPP
#define CFN_VERSION_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "cfn/core/api.hpp"

#define CFN_VERSION_MAJOR 1
#define CFN_VERSION_MINOR 0
#define CFN_VERSION_PATCH 0

namespace cfn {

inline constexpr std::uint32_t version_major = CFN_VERSION_MAJOR;
inline constexpr std::uint32_t version_minor = CFN_VERSION_MINOR;
inline constexpr std::uint32_t version_patch = CFN_VERSION_PATCH;
inline constexpr std::string_view version_string = "1.0.0";

/// Persistence and wire format version. Bumped whenever a stored or framed
/// layout changes incompatibly.
inline constexpr std::uint16_t format_version = 1;

/// Full identification banner, including compiler and platform of the build.
[[nodiscard]] CFN_API std::string build_banner();

}  // namespace cfn

#endif  // CFN_VERSION_HPP
