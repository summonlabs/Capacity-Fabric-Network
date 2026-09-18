// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/engine/prediction.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "cfn/core/checked.hpp"

namespace cfn {

namespace {

constexpr std::int64_t kNanosPerSecond = 1000000000LL;
constexpr std::int64_t kSecondsPerHour = 3600LL;

struct SeriesOutcome {
  bool available = false;
  std::string reason;
  std::uint64_t base = 0;
  std::uint64_t predicted = 0;
  std::int64_t slope_per_hour = 0;
  std::uint32_t sample_count = 0;
  Duration extrapolation{};
  std::uint32_t confidence_ppm = 0;
  bool slope_clamped = false;
  bool range_clamped = false;
  bool trend_fallback = false;
  std::vector<std::string> notes;
};

[[nodiscard]] std::int64_t difference_seconds(Timestamp later, Timestamp earlier, bool& ok) {
  std::int64_t delta = 0;
  if (!checked::sub_i64(later.unix_nanos, earlier.unix_nanos, delta)) {
    ok = false;
    return 0;
  }
  ok = true;
  return delta / kNanosPerSecond;
}

[[nodiscard]] std::uint32_t factor_from_ratio(std::int64_t value, std::int64_t limit) {
  if (limit <= 0) {
    return 0;
  }
  if (value <= 0) {
    return 1000000U;
  }
  if (value >= limit) {
    return 0U;
  }
  const std::int64_t ratio = (value * 1000000LL) / limit;
  if (ratio <= 0) {
    return 1000000U;
  }
  if (ratio >= 1000000LL) {
    return 0U;
  }
  return static_cast<std::uint32_t>(1000000LL - ratio);
}

/// Least squares slope in capacity units per hour, computed in integers with an
/// optional divisor applied to the observations so extreme magnitudes degrade
/// to a coarser fit instead of overflowing.
[[nodiscard]] bool least_squares_slope(const std::vector<std::pair<std::int64_t, std::int64_t>>& points,
                                       std::int64_t divisor, std::int64_t& slope_per_second) {
  const std::int64_t count = static_cast<std::int64_t>(points.size());
  std::int64_t sum_x = 0;
  std::int64_t sum_y = 0;
  std::int64_t sum_xx = 0;
  std::int64_t sum_xy = 0;
  for (const auto& point : points) {
    const std::int64_t x = point.first;
    const std::int64_t y = point.second / divisor;
    std::int64_t next = 0;
    if (!checked::add_i64(sum_x, x, next)) return false;
    sum_x = next;
    if (!checked::add_i64(sum_y, y, next)) return false;
    sum_y = next;
    if (!checked::mul_i64(x, x, next)) return false;
    if (!checked::add_i64(sum_xx, next, next)) return false;
    sum_xx = next;
    if (!checked::mul_i64(x, y, next)) return false;
    if (!checked::add_i64(sum_xy, next, next)) return false;
    sum_xy = next;
  }
  std::int64_t left = 0;
  if (!checked::mul_i64(count, sum_xy, left)) return false;
  std::int64_t right = 0;
  if (!checked::mul_i64(sum_x, sum_y, right)) return false;
  std::int64_t numerator = 0;
  if (!checked::sub_i64(left, right, numerator)) return false;

  std::int64_t left_den = 0;
  if (!checked::mul_i64(count, sum_xx, left_den)) return false;
  std::int64_t sum_x_squared = 0;
  if (!checked::mul_i64(sum_x, sum_x, sum_x_squared)) return false;
  std::int64_t denominator = 0;
  if (!checked::sub_i64(left_den, sum_x_squared, denominator)) return false;
  if (denominator == 0) {
    return false;
  }
  slope_per_second = numerator / denominator;
  return true;
}

[[nodiscard]] SeriesOutcome evaluate_series(const std::vector<const CapacityObservation*>& samples,
                                            const PredictionRequest& request, Timestamp produced_at,
                                            const Limits& limits) {
  SeriesOutcome outcome;
  outcome.sample_count = static_cast<std::uint32_t>(samples.size());
  if (samples.empty()) {
    outcome.reason = "no authoritative observations";
    return outcome;
  }
  if (outcome.sample_count < request.min_samples) {
    outcome.reason = "fewer authoritative observations than the declared minimum";
    return outcome;
  }
  outcome.base = samples.back()->observed_usable.units;

  const bool has_horizon = request.horizon.unix_nanos != 0;
  Timestamp horizon = request.horizon;
  if (!has_horizon) {
    horizon = produced_at;
  }
  bool ok = false;
  const std::int64_t horizon_seconds = difference_seconds(horizon, samples.back()->at, ok);
  if (!ok) {
    outcome.reason = "prediction horizon is not representable";
    return outcome;
  }
  if (horizon_seconds < 0) {
    outcome.reason = "prediction horizon precedes the newest observation";
    return outcome;
  }
  outcome.extrapolation = Duration::from_seconds(horizon_seconds);
  if (outcome.extrapolation.nanos > request.max_extrapolation.nanos) {
    outcome.reason = "prediction horizon is beyond the declared maximum extrapolation";
    return outcome;
  }

  std::int64_t slope_per_second = 0;
  switch (request.kind) {
    case PredictionModelKind::None: {
      outcome.reason = "no prediction model was selected";
      return outcome;
    }
    case PredictionModelKind::LastValue: {
      outcome.notes.emplace_back("model=last-value: the newest authoritative observation is repeated");
      break;
    }
    case PredictionModelKind::BoundedLinearTrend: {
      std::vector<std::pair<std::int64_t, std::int64_t>> points;
      points.reserve(samples.size());
      const Timestamp origin = samples.front()->at;
      for (const CapacityObservation* sample : samples) {
        bool point_ok = false;
        const std::int64_t x = difference_seconds(sample->at, origin, point_ok);
        if (!point_ok) {
          outcome.reason = "observation timestamps are not representable";
          return outcome;
        }
        std::int64_t y = 0;
        if (!checked::fits_i64(sample->observed_usable.units, y)) {
          outcome.reason = "observation magnitude exceeds the supported range";
          return outcome;
        }
        points.emplace_back(x, y);
      }
      bool fitted = false;
      static const std::int64_t kDivisors[] = {1, 1000, 1000000, 1000000000};
      for (const std::int64_t divisor : kDivisors) {
        if (least_squares_slope(points, divisor, slope_per_second)) {
          if (divisor != 1) {
            std::int64_t scaled = 0;
            if (checked::mul_i64(slope_per_second, divisor, scaled)) {
              slope_per_second = scaled;
              outcome.notes.emplace_back(
                  "model=bounded-linear-trend: observations were rescaled to keep the fit exact");
            } else {
              continue;
            }
          }
          fitted = true;
          break;
        }
      }
      if (!fitted) {
        outcome.trend_fallback = true;
        slope_per_second = 0;
        outcome.notes.emplace_back(
            "model=bounded-linear-trend fell back to last value: the fit overflowed the integer range");
      } else {
        outcome.notes.emplace_back("model=bounded-linear-trend: integer least squares over the window");
      }
      break;
    }
    case PredictionModelKind::EwmaLevel: {
      const std::uint32_t alpha = std::min(request.ewma_alpha_ppm, 1000000U);
      std::int64_t level = 0;
      if (!checked::fits_i64(samples.front()->observed_usable.units, level)) {
        outcome.reason = "observation magnitude exceeds the supported range";
        return outcome;
      }
      for (std::size_t index = 1; index < samples.size(); ++index) {
        std::int64_t value = 0;
        if (!checked::fits_i64(samples[index]->observed_usable.units, value)) {
          outcome.reason = "observation magnitude exceeds the supported range";
          return outcome;
        }
        std::int64_t delta = 0;
        if (!checked::sub_i64(value, level, delta)) {
          outcome.reason = "observation delta exceeds the supported range";
          return outcome;
        }
        std::int64_t step = (delta * static_cast<std::int64_t>(alpha)) / 1000000LL;
        if (!checked::add_i64(level, step, level)) {
          outcome.reason = "smoothed level exceeds the supported range";
          return outcome;
        }
      }
      if (level < 0) {
        level = 0;
      }
      outcome.base = static_cast<std::uint64_t>(level);
      outcome.notes.emplace_back("model=ewma-level: exponentially weighted level of the window");
      break;
    }
  }

  // --- clamp the slope ------------------------------------------------------
  const std::int64_t max_slope_per_hour = static_cast<std::int64_t>(request.max_growth_per_hour.units);
  std::int64_t slope_per_hour = (slope_per_second * kSecondsPerHour);
  if (max_slope_per_hour > 0) {
    if (slope_per_hour > max_slope_per_hour) {
      slope_per_hour = max_slope_per_hour;
      outcome.slope_clamped = true;
    } else if (slope_per_hour < -max_slope_per_hour) {
      slope_per_hour = -max_slope_per_hour;
      outcome.slope_clamped = true;
    }
  }
  outcome.slope_per_hour = slope_per_hour;
  if (outcome.slope_clamped) {
    outcome.notes.emplace_back("slope was clamped to the declared maximum growth rate");
  }

  // --- extrapolate ----------------------------------------------------------
  std::int64_t base_signed = 0;
  if (!checked::fits_i64(outcome.base, base_signed)) {
    outcome.reason = "base observation exceeds the supported range";
    return outcome;
  }
  const std::int64_t extrapolation_hours_nanos = outcome.extrapolation.nanos;
  std::int64_t projected = base_signed;
  {
    std::int64_t delta = (slope_per_hour * extrapolation_hours_nanos) / (kSecondsPerHour * kNanosPerSecond);
    if (!checked::add_i64(base_signed, delta, projected)) {
      projected = delta > 0 ? static_cast<std::int64_t>(limits.max_capacity_units) : 0;
      outcome.range_clamped = true;
    }
  }

  // --- clamp the total movement --------------------------------------------
  std::uint64_t allowance = 0;
  if (!checked::scale_ppm(outcome.base, request.max_extrapolation_ppm, allowance)) {
    outcome.reason = "movement allowance overflows the supported range";
    return outcome;
  }
  const std::int64_t allowance_signed = static_cast<std::int64_t>(allowance);
  const std::uint64_t ceiling =
      checked::saturating_add(outcome.base, static_cast<std::uint64_t>(allowance_signed));
  const std::uint64_t floor = outcome.base > static_cast<std::uint64_t>(allowance_signed)
                                  ? outcome.base - static_cast<std::uint64_t>(allowance_signed)
                                  : 0ULL;
  if (projected < 0) {
    projected = 0;
    outcome.range_clamped = true;
  }
  if (static_cast<std::uint64_t>(projected) > ceiling) {
    projected = static_cast<std::int64_t>(ceiling);
    outcome.range_clamped = true;
  }
  if (static_cast<std::uint64_t>(projected) < floor) {
    projected = static_cast<std::int64_t>(floor);
    outcome.range_clamped = true;
  }
  if (outcome.range_clamped) {
    outcome.notes.emplace_back("projected value was clamped to the declared movement allowance");
  }
  if (static_cast<std::uint64_t>(projected) > limits.max_capacity_units) {
    outcome.reason = "projected value exceeds the configured maximum capacity";
    return outcome;
  }

  outcome.predicted = static_cast<std::uint64_t>(projected);
  outcome.available = true;

  // --- confidence -----------------------------------------------------------
  std::uint32_t count_factor = 1000000U;
  if (outcome.sample_count < 8U) {
    count_factor = static_cast<std::uint32_t>((static_cast<std::uint64_t>(outcome.sample_count) *
                                               1000000ULL) / 8ULL);
  }
  bool age_ok = false;
  const std::int64_t newest_age = difference_seconds(produced_at, samples.back()->at, age_ok);
  std::uint32_t age_factor = 1000000U;
  if (age_ok) {
    age_factor = factor_from_ratio(newest_age, request.max_sample_age.nanos / kNanosPerSecond);
  }
  const std::uint32_t horizon_factor = factor_from_ratio(
      horizon_seconds, request.max_extrapolation.nanos / kNanosPerSecond);
  std::uint64_t confidence = (static_cast<std::uint64_t>(count_factor) * age_factor) / 1000000ULL;
  confidence = (confidence * horizon_factor) / 1000000ULL;
  if (outcome.slope_clamped || outcome.range_clamped) {
    confidence = confidence / 2ULL;
  }
  if (outcome.trend_fallback) {
    confidence = confidence / 2ULL;
  }
  outcome.confidence_ppm = static_cast<std::uint32_t>(confidence);
  return outcome;
}

}  // namespace

std::string_view to_string(PredictionModelKind kind) noexcept {
  switch (kind) {
    case PredictionModelKind::None: return "none";
    case PredictionModelKind::LastValue: return "last-value";
    case PredictionModelKind::BoundedLinearTrend: return "bounded-linear-trend";
    case PredictionModelKind::EwmaLevel: return "ewma-level";
  }
  return "none";
}

bool prediction_model_from_string(std::string_view text, PredictionModelKind& out) noexcept {
  if (text == "none") { out = PredictionModelKind::None; return true; }
  if (text == "last-value") { out = PredictionModelKind::LastValue; return true; }
  if (text == "bounded-linear-trend") { out = PredictionModelKind::BoundedLinearTrend; return true; }
  if (text == "ewma-level") { out = PredictionModelKind::EwmaLevel; return true; }
  return false;
}

Outcome<PredictionResult> predict(const PredictionRequest& request, const Limits& limits,
                                  Timestamp produced_at) {
  CFN_RETURN_IF_ERROR(limits.validate());
  if (request.kind == PredictionModelKind::None) {
    return Error(ErrorCode::InvalidArgument, "prediction request selects no model");
  }
  if (request.min_samples == 0) {
    return Error(ErrorCode::InvalidArgument, "prediction request requires at least one sample");
  }
  if (request.history.size() > limits.max_prediction_samples * 16ULL) {
    return Error(ErrorCode::LimitExceeded, "prediction history exceeds the configured maximum");
  }
  if (request.max_extrapolation.nanos <= 0) {
    return Error(ErrorCode::InvalidArgument, "prediction request declares no extrapolation allowance");
  }
  if (request.max_sample_age.nanos <= 0) {
    return Error(ErrorCode::InvalidArgument, "prediction request declares no sample age allowance");
  }

  PredictionResult result;
  result.kind = request.kind;
  result.produced_at = produced_at;
  result.horizon = request.horizon.unix_nanos == 0 ? produced_at : request.horizon;
  result.input_generations.fabric_epoch = request.epoch;
  result.input_generations.computed_at = produced_at;

  const auto source_id = EvidenceSourceId::parse("cfn-predictor");
  if (!source_id.has_value()) {
    return Error(ErrorCode::InvalidState, "predictor source identity template is invalid");
  }
  result.provenance = predicted_provenance(*source_id, produced_at, request.epoch);
  result.input_generations.provenance = result.provenance;

  // --- group authoritative observations by resource -------------------------
  std::vector<const CapacityObservation*> accepted;
  accepted.reserve(request.history.size());
  std::uint32_t rejected_class = 0;
  std::uint32_t rejected_age = 0;
  std::uint32_t rejected_future = 0;
  for (const CapacityObservation& observation : request.history) {
    if (!may_be_authoritative(observation.evidence_class) || !observation.provenance.authoritative) {
      rejected_class += 1U;
      continue;
    }
    if (!request.resource.valid() || observation.resource == request.resource) {
      // keep
    } else {
      continue;
    }
    bool ok = false;
    const std::int64_t age = difference_seconds(produced_at, observation.at, ok);
    if (!ok || age < 0) {
      rejected_future += 1U;
      continue;
    }
    if (age * kNanosPerSecond > request.max_sample_age.nanos) {
      rejected_age += 1U;
      continue;
    }
    accepted.push_back(&observation);
  }

  std::sort(accepted.begin(), accepted.end(),
            [](const CapacityObservation* lhs, const CapacityObservation* rhs) {
              if (lhs->at != rhs->at) {
                return lhs->at < rhs->at;
              }
              return lhs->resource < rhs->resource;
            });

  if (accepted.size() > limits.max_prediction_samples) {
    accepted.erase(accepted.begin(),
                   accepted.end() - static_cast<std::ptrdiff_t>(limits.max_prediction_samples));
    result.assumptions.emplace_back("history was truncated to the configured sample bound");
  }

  std::vector<std::pair<ResourceId, std::vector<const CapacityObservation*>>> groups;
  for (const CapacityObservation* observation : accepted) {
    if (!groups.empty() && groups.back().first == observation->resource) {
      groups.back().second.push_back(observation);
      continue;
    }
    auto existing = std::find_if(groups.begin(), groups.end(), [&](const auto& entry) {
      return entry.first == observation->resource;
    });
    if (existing != groups.end()) {
      existing->second.push_back(observation);
    } else {
      groups.emplace_back(observation->resource, std::vector<const CapacityObservation*>{observation});
    }
  }

  result.assumptions.emplace_back(std::string("model=").append(to_string(request.kind)));
  result.assumptions.emplace_back(std::string("minimum authoritative samples=")
                                      .append(std::to_string(request.min_samples)));
  result.assumptions.emplace_back(std::string("records rejected: wrong evidence class=")
                                      .append(std::to_string(rejected_class))
                                      .append(", outside the age window=")
                                      .append(std::to_string(rejected_age))
                                      .append(", future dated=")
                                      .append(std::to_string(rejected_future)));

  if (groups.empty()) {
    result.available = false;
    result.unavailable_reason = "no authoritative observations fall inside the evidence window";
    return result;
  }

  std::uint64_t predicted_total = 0;
  std::uint64_t base_total = 0;
  std::uint64_t confidence_total = 0;
  std::uint32_t contributing = 0;
  for (const auto& group : groups) {
    const SeriesOutcome series = evaluate_series(group.second, request, produced_at, limits);
    ResourcePrediction entry;
    entry.resource = group.first;
    entry.available = series.available;
    entry.base_observed = Capacity::from_units(series.base);
    entry.predicted_available = Capacity::from_units(series.predicted);
    entry.slope_per_hour = series.slope_per_hour;
    entry.sample_count = series.sample_count;
    entry.extrapolation = series.extrapolation;
    entry.confidence_ppm = series.confidence_ppm;
    entry.slope_clamped = series.slope_clamped;
    entry.range_clamped = series.range_clamped;
    result.per_resource.push_back(entry);
    if (!series.available) {
      continue;
    }
    std::uint64_t next = 0;
    if (!checked::add_u64(predicted_total, series.predicted, next)) {
      return Error(ErrorCode::Overflow, "aggregate prediction overflows the supported range");
    }
    predicted_total = next;
    if (!checked::add_u64(base_total, series.base, next)) {
      return Error(ErrorCode::Overflow, "aggregate base observation overflows the supported range");
    }
    base_total = next;
    confidence_total += series.confidence_ppm;
    contributing += 1U;
  }

  if (contributing == 0) {
    result.available = false;
    result.unavailable_reason =
        "every resource series failed the minimum sample or window requirement";
    return result;
  }

  result.available = true;
  result.evidence_class = EvidenceClass::Predicted;
  result.predicted_available = Capacity::from_units(predicted_total);
  result.base_observed = Capacity::from_units(base_total);
  result.confidence_ppm = static_cast<std::uint32_t>(confidence_total / contributing);
  if (predicted_total > limits.max_capacity_units) {
    result.available = false;
    result.unavailable_reason = "aggregate prediction exceeds the configured maximum capacity";
  }
  return result;
}

}  // namespace cfn
