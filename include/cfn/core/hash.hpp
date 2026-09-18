// Capacity Fabric Network - deterministic hashing.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CORE_HASH_HPP
#define CFN_CORE_HASH_HPP

#include <cstdint>
#include <string_view>

#include "cfn/core/identity.hpp"

namespace cfn::hash {

/// FNV-1a 64. Stable across platforms and standard libraries; used for
/// digests that must be reproducible, never for security.
[[nodiscard]] constexpr std::uint64_t fnv1a(std::string_view text) noexcept {
  std::uint64_t value = 1469598103934665603ULL;
  for (const char character : text) {
    value ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
    value *= 1099511628211ULL;
  }
  return value;
}

/// SplitMix64 finaliser: strong avalanche, fully deterministic.
[[nodiscard]] constexpr std::uint64_t mix(std::uint64_t value) noexcept {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31);
}

[[nodiscard]] constexpr std::uint64_t combine(std::uint64_t seed, std::uint64_t value) noexcept {
  return mix(seed ^ mix(value));
}

[[nodiscard]] inline std::uint64_t combine_text(std::uint64_t seed, std::string_view text) noexcept {
  return combine(seed, fnv1a(text));
}

/// Folds a kind tag and identity into a digest accumulator.
template <class Id>
[[nodiscard]] std::uint64_t combine_id(std::uint64_t seed, const Id& id) noexcept {
  return combine(seed, id.hash());
}

}  // namespace cfn::hash

#endif  // CFN_CORE_HASH_HPP
