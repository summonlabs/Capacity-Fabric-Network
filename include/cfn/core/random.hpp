// Capacity Fabric Network - deterministic pseudo random generation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Property, randomized, and benchmark populations all draw from this
// generator. It is PCG32 with a SplitMix64 seeder: reproducible bit for bit
// on every platform, so a failing seed can always be replayed.
#ifndef CFN_CORE_RANDOM_HPP
#define CFN_CORE_RANDOM_HPP

#include <cstdint>
#include <vector>

#include "cfn/core/api.hpp"

namespace cfn {

class CFN_API Rng {
 public:
  explicit Rng(std::uint64_t seed = 0x2545F4914F6CDD1DULL, std::uint64_t stream = 0xDA3E39CB94B95BDBULL) noexcept;

  /// Raw 32-bit draw.
  [[nodiscard]] std::uint32_t next_u32() noexcept;
  /// Raw 64-bit draw built from two 32-bit draws (high word first).
  [[nodiscard]] std::uint64_t next_u64() noexcept;

  /// Uniform value in [0, bound). Returns 0 when bound is 0. Uses rejection
  /// sampling so the distribution does not depend on the modulus.
  [[nodiscard]] std::uint64_t bounded(std::uint64_t bound) noexcept;
  /// Uniform value in [low, high]; returns low when high <= low.
  [[nodiscard]] std::uint64_t range(std::uint64_t low, std::uint64_t high) noexcept;

  /// True with probability ppm / 1e6.
  [[nodiscard]] bool chance(std::uint32_t ppm) noexcept;

  /// Deterministically shuffles the range [0, count) using a Fisher-Yates
  /// pass driven by this generator.
  void shuffle(std::vector<std::uint32_t>& values) noexcept;

  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
  [[nodiscard]] std::uint64_t stream() const noexcept { return stream_; }

  void reseed(std::uint64_t seed, std::uint64_t stream) noexcept;

 private:
  std::uint64_t state_ = 0;
  std::uint64_t increment_ = 1;
  std::uint64_t seed_ = 0;
  std::uint64_t stream_ = 0;
};

}  // namespace cfn

#endif  // CFN_CORE_RANDOM_HPP
