// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/core/time.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace cfn {

bool elapsed(Timestamp later, Timestamp earlier, Duration& out) noexcept {
  return checked::sub_i64(later.unix_nanos, earlier.unix_nanos, out.nanos);
}

bool elapsed(Instant later, Instant earlier, Duration& out) noexcept {
  return checked::sub_i64(later.nanos, earlier.nanos, out.nanos);
}

namespace {

/// Largest year the ISO 8601 rendering supports.
constexpr std::int64_t kMinYear = 1;
constexpr std::int64_t kMaxYear = 9999;

/// Floor division that rounds towards negative infinity for negative values.
[[nodiscard]] std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
  const std::int64_t quotient = value / divisor;
  const std::int64_t remainder = value % divisor;
  return (remainder != 0 && ((remainder < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

/// Civil date from a count of days since 1970-01-01. Integer only, so the
/// rendering is identical on every platform and independent of the C library
/// time zone and range limitations.
void civil_from_days(std::int64_t days, std::int64_t& year, unsigned& month, unsigned& day) noexcept {
  const std::int64_t shifted = days + 719468;
  const std::int64_t era = floor_div(shifted, 146097);
  const std::int64_t day_of_era = shifted - (era * 146097);
  const std::int64_t year_of_era =
      (day_of_era - (day_of_era / 1460) + (day_of_era / 36524) - (day_of_era / 146096)) / 365;
  std::int64_t value = year_of_era + (era * 400);
  const std::int64_t day_of_year =
      day_of_era - ((365 * year_of_era) + (year_of_era / 4) - (year_of_era / 100));
  const std::int64_t month_prime = ((5 * day_of_year) + 2) / 153;
  day = static_cast<unsigned>(day_of_year - (((153 * month_prime) + 2) / 5) + 1);
  const std::int64_t raw_month = month_prime + (month_prime < 10 ? 3 : -9);
  month = static_cast<unsigned>(raw_month);
  year = value + (raw_month <= 2 ? 1 : 0);
}

}  // namespace

std::string to_iso8601(Timestamp value) {
  const std::int64_t days = floor_div(value.unix_nanos, 86400000000000LL);
  const std::int64_t remainder = value.unix_nanos - (days * 86400000000000LL);
  const std::int64_t seconds_of_day = remainder / 1000000000LL;
  const std::int64_t nanos_part = remainder % 1000000000LL;
  std::int64_t year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civil_from_days(days, year, month, day);
  if (year < kMinYear || year > kMaxYear) {
    return "invalid-timestamp";
  }
  const std::int64_t hour = seconds_of_day / 3600;
  const std::int64_t minute = (seconds_of_day % 3600) / 60;
  const std::int64_t second = seconds_of_day % 60;
  char buffer[48] = {};
  const int written =
      std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02uT%02lld:%02lld:%02lld.%09lldZ",
                    static_cast<long long>(year), month, day, static_cast<long long>(hour),
                    static_cast<long long>(minute), static_cast<long long>(second),
                    static_cast<long long>(nanos_part));
  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
    return "invalid-timestamp";
  }
  return std::string(buffer, static_cast<std::size_t>(written));
}

Clock::~Clock() = default;

Timestamp SystemClock::wall_now() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  return Timestamp::from_unix_nanos(static_cast<std::int64_t>(nanos));
}

Instant SystemClock::mono_now() const {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  return Instant{static_cast<std::int64_t>(nanos)};
}

ManualClock::ManualClock(Timestamp wall_start, Instant mono_start) : wall_(wall_start), mono_(mono_start) {}

Timestamp ManualClock::wall_now() const { return wall_; }
Instant ManualClock::mono_now() const { return mono_; }

void ManualClock::advance(Duration delta) noexcept {
  std::int64_t next_wall = 0;
  if (checked::add_i64(wall_.unix_nanos, delta.nanos, next_wall)) {
    wall_.unix_nanos = next_wall;
  }
  std::int64_t next_mono = 0;
  if (checked::add_i64(mono_.nanos, delta.nanos, next_mono)) {
    mono_.nanos = next_mono;
  }
}

void ManualClock::set_wall(Timestamp value) noexcept { wall_ = value; }

std::shared_ptr<Clock> make_system_clock() { return std::make_shared<SystemClock>(); }

std::shared_ptr<ManualClock> make_manual_clock(Timestamp wall_start, Instant mono_start) {
  return std::make_shared<ManualClock>(wall_start, mono_start);
}

}  // namespace cfn