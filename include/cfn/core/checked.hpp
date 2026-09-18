// Capacity Fabric Network - checked integer arithmetic.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CORE_CHECKED_HPP
#define CFN_CORE_CHECKED_HPP

#include <cstdint>
#include <limits>

namespace cfn::checked {

using u64 = std::uint64_t;
using i64 = std::int64_t;
using u32 = std::uint32_t;
using i32 = std::int32_t;

inline constexpr u64 kU64Max = std::numeric_limits<u64>::max();
inline constexpr i64 kI64Max = std::numeric_limits<i64>::max();
inline constexpr i64 kI64Min = std::numeric_limits<i64>::min();

/// Fixed point scale used for deterministic fractional arithmetic.
inline constexpr i64 kPpmScale = 1000000;

[[nodiscard]] constexpr bool add_u64(u64 a, u64 b, u64& out) noexcept {
  if (a > kU64Max - b) {
    return false;
  }
  out = a + b;
  return true;
}

[[nodiscard]] constexpr bool sub_u64(u64 a, u64 b, u64& out) noexcept {
  if (b > a) {
    return false;
  }
  out = a - b;
  return true;
}

[[nodiscard]] constexpr bool mul_u64(u64 a, u64 b, u64& out) noexcept {
  if (a != 0 && b > kU64Max / a) {
    return false;
  }
  out = a * b;
  return true;
}

/// Computes floor(a * b / d) without forming the overflowing intermediate
/// product. Requires d != 0.
[[nodiscard]] constexpr bool mul_div_u64(u64 a, u64 b, u64 d, u64& out) noexcept {
  if (d == 0) {
    return false;
  }
  const u64 quotient = a / d;
  const u64 remainder = a % d;
  u64 high = 0;
  if (!mul_u64(quotient, b, high)) {
    return false;
  }
  u64 low = 0;
  if (!mul_u64(remainder, b, low)) {
    return false;
  }
  const u64 low_scaled = low / d;
  u64 sum = 0;
  if (!add_u64(high, low_scaled, sum)) {
    return false;
  }
  out = sum;
  return true;
}

/// floor(value * ppm / 1e6) with full overflow checking.
[[nodiscard]] constexpr bool scale_ppm(u64 value, u32 ppm, u64& out) noexcept {
  return mul_div_u64(value, static_cast<u64>(ppm), static_cast<u64>(kPpmScale), out);
}

[[nodiscard]] constexpr bool add_i64(i64 a, i64 b, i64& out) noexcept {
  if (b > 0 && a > kI64Max - b) {
    return false;
  }
  if (b < 0 && a < kI64Min - b) {
    return false;
  }
  out = a + b;
  return true;
}

[[nodiscard]] constexpr bool sub_i64(i64 a, i64 b, i64& out) noexcept {
  if (b == kI64Min) {
    return false;
  }
  return add_i64(a, -b, out);
}

[[nodiscard]] constexpr bool mul_i64(i64 a, i64 b, i64& out) noexcept {
  if (a == 0 || b == 0) {
    out = 0;
    return true;
  }
  if ((a == -1 && b == kI64Min) || (b == -1 && a == kI64Min)) {
    return false;
  }
  const i64 product = a * b;
  if (product / b != a) {
    return false;
  }
  out = product;
  return true;
}

[[nodiscard]] constexpr bool fits_u64(i64 value, u64& out) noexcept {
  if (value < 0) {
    return false;
  }
  out = static_cast<u64>(value);
  return true;
}

[[nodiscard]] constexpr bool fits_i64(u64 value, i64& out) noexcept {
  if (value > static_cast<u64>(kI64Max)) {
    return false;
  }
  out = static_cast<i64>(value);
  return true;
}

[[nodiscard]] constexpr bool narrow_u32(u64 value, u32& out) noexcept {
  if (value > static_cast<u64>(std::numeric_limits<u32>::max())) {
    return false;
  }
  out = static_cast<u32>(value);
  return true;
}

[[nodiscard]] constexpr bool narrow_u16(u64 value, std::uint16_t& out) noexcept {
  if (value > static_cast<u64>(std::numeric_limits<std::uint16_t>::max())) {
    return false;
  }
  out = static_cast<std::uint16_t>(value);
  return true;
}

[[nodiscard]] constexpr u64 saturating_add(u64 a, u64 b) noexcept {
  const u64 sum = a + b;
  return sum < a ? kU64Max : sum;
}

/// Running sum that records whether any term overflowed, so a caller can
/// distinguish a genuine zero from a meaningless one.
class Accumulator {
 public:
  constexpr Accumulator() noexcept = default;

  [[nodiscard]] constexpr bool add(u64 term) noexcept {
    u64 next = 0;
    if (!add_u64(total_, term, next)) {
      overflowed_ = true;
      return false;
    }
    total_ = next;
    return true;
  }

  [[nodiscard]] constexpr bool subtract(u64 term) noexcept {
    u64 next = 0;
    if (!sub_u64(total_, term, next)) {
      underflowed_ = true;
      return false;
    }
    total_ = next;
    return true;
  }

  [[nodiscard]] constexpr u64 value() const noexcept { return total_; }
  [[nodiscard]] constexpr bool overflowed() const noexcept { return overflowed_; }
  [[nodiscard]] constexpr bool underflowed() const noexcept { return underflowed_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return !overflowed_ && !underflowed_; }
  constexpr void reset() noexcept {
    total_ = 0;
    overflowed_ = false;
    underflowed_ = false;
  }

 private:
  u64 total_ = 0;
  bool overflowed_ = false;
  bool underflowed_ = false;
};

}  // namespace cfn::checked

#endif  // CFN_CORE_CHECKED_HPP
