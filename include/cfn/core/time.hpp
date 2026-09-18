// Capacity Fabric Network - clocks and time values.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CORE_TIME_HPP
#define CFN_CORE_TIME_HPP

#include <compare>
#include <cstdint>
#include <memory>
#include <string>

#include "cfn/core/api.hpp"
#include "cfn/core/checked.hpp"

namespace cfn {

struct Timestamp {
  std::int64_t unix_nanos = 0;

  [[nodiscard]] static constexpr Timestamp from_unix_nanos(std::int64_t value) noexcept {
    return Timestamp{value};
  }
  /// Converts whole seconds to nanoseconds, saturating at the representable
  /// limits instead of overflowing.
  [[nodiscard]] static constexpr Timestamp from_unix_seconds(std::int64_t value) noexcept {
    constexpr std::int64_t kLimit = 9223372036LL;
    if (value > kLimit) {
      return Timestamp{9223372036854775807LL};
    }
    if (value < -kLimit) {
      return Timestamp{-9223372036854775807LL - 1LL};
    }
    return Timestamp{value * 1000000000LL};
  }
  [[nodiscard]] friend constexpr bool operator==(Timestamp, Timestamp) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Timestamp, Timestamp) noexcept = default;
};

struct Duration {
  std::int64_t nanos = 0;

  [[nodiscard]] static constexpr Duration from_nanos(std::int64_t value) noexcept { return Duration{value}; }
  [[nodiscard]] static constexpr Duration from_micros(std::int64_t value) noexcept { return Duration{value * 1000LL}; }
  [[nodiscard]] static constexpr Duration from_millis(std::int64_t value) noexcept { return Duration{value * 1000000LL}; }
  [[nodiscard]] static constexpr Duration from_seconds(std::int64_t value) noexcept { return Duration{value * 1000000000LL}; }
  [[nodiscard]] static constexpr Duration from_minutes(std::int64_t value) noexcept { return Duration{value * 60000000000LL}; }
  [[nodiscard]] static constexpr Duration from_hours(std::int64_t value) noexcept { return Duration{value * 3600000000000LL}; }

  [[nodiscard]] constexpr bool is_positive() const noexcept { return nanos > 0; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return nanos == 0; }
  [[nodiscard]] friend constexpr bool operator==(Duration, Duration) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Duration, Duration) noexcept = default;
};

struct Instant {
  std::int64_t nanos = 0;

  [[nodiscard]] friend constexpr bool operator==(Instant, Instant) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Instant, Instant) noexcept = default;
};

[[nodiscard]] CFN_API bool elapsed(Timestamp later, Timestamp earlier, Duration& out) noexcept;
[[nodiscard]] CFN_API bool elapsed(Instant later, Instant earlier, Duration& out) noexcept;
[[nodiscard]] CFN_API std::string to_iso8601(Timestamp value);

class CFN_API Clock {
 public:
  Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  virtual ~Clock();

  [[nodiscard]] virtual Timestamp wall_now() const = 0;
  [[nodiscard]] virtual Instant mono_now() const = 0;
};

class CFN_API SystemClock final : public Clock {
 public:
  [[nodiscard]] Timestamp wall_now() const override;
  [[nodiscard]] Instant mono_now() const override;
};

class CFN_API ManualClock final : public Clock {
 public:
  explicit ManualClock(Timestamp wall_start = Timestamp{}, Instant mono_start = Instant{});

  [[nodiscard]] Timestamp wall_now() const override;
  [[nodiscard]] Instant mono_now() const override;

  void advance(Duration delta) noexcept;
  void set_wall(Timestamp value) noexcept;

 private:
  Timestamp wall_;
  Instant mono_;
};

[[nodiscard]] CFN_API std::shared_ptr<Clock> make_system_clock();
[[nodiscard]] CFN_API std::shared_ptr<ManualClock> make_manual_clock(
    Timestamp wall_start = Timestamp{}, Instant mono_start = Instant{});

}  // namespace cfn

#endif  // CFN_CORE_TIME_HPP
