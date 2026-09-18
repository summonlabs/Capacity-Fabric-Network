// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/core/random.hpp"

#include <utility>

namespace cfn {

namespace {

constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;

}  // namespace

Rng::Rng(std::uint64_t seed, std::uint64_t stream) noexcept { reseed(seed, stream); }

void Rng::reseed(std::uint64_t seed, std::uint64_t stream) noexcept {
  seed_ = seed;
  stream_ = stream;
  state_ = 0;
  increment_ = (stream << 1U) | 1U;
  (void)next_u32();
  state_ += seed;
  (void)next_u32();
}

std::uint32_t Rng::next_u32() noexcept {
  const std::uint64_t previous = state_;
  state_ = (previous * kMultiplier) + increment_;
  const std::uint32_t xorshifted = static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
  const std::uint32_t rotation = static_cast<std::uint32_t>(previous >> 59U);
  return (xorshifted >> rotation) | (xorshifted << ((~rotation + 1U) & 31U));
}

std::uint64_t Rng::next_u64() noexcept {
  const std::uint64_t high = static_cast<std::uint64_t>(next_u32());
  const std::uint64_t low = static_cast<std::uint64_t>(next_u32());
  return (high << 32U) | low;
}

std::uint64_t Rng::bounded(std::uint64_t bound) noexcept {
  if (bound == 0) {
    return 0;
  }
  // Largest multiple of bound representable in 64 bits; draws below it are
  // discarded so the result is exactly uniform.
  const std::uint64_t threshold = (~bound + 1ULL) % bound;
  for (;;) {
    const std::uint64_t draw = next_u64();
    if (draw >= threshold) {
      return draw % bound;
    }
  }
}

std::uint64_t Rng::range(std::uint64_t low, std::uint64_t high) noexcept {
  if (high <= low) {
    return low;
  }
  const std::uint64_t span = high - low;
  if (span == UINT64_MAX) {
    return next_u64();
  }
  return low + bounded(span + 1ULL);
}

bool Rng::chance(std::uint32_t ppm) noexcept {
  if (ppm == 0) {
    return false;
  }
  if (ppm >= 1000000U) {
    return true;
  }
  return bounded(1000000ULL) < static_cast<std::uint64_t>(ppm);
}

void Rng::shuffle(std::vector<std::uint32_t>& values) noexcept {
  if (values.size() < 2) {
    return;
  }
  for (std::size_t index = values.size() - 1; index > 0; --index) {
    const std::uint64_t pick = bounded(static_cast<std::uint64_t>(index) + 1ULL);
    std::swap(values[index], values[static_cast<std::size_t>(pick)]);
  }
}

}  // namespace cfn
