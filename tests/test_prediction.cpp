// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <string>
#include <vector>

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

[[nodiscard]] CapacityObservation observation(const char* resource, std::int64_t seconds_ago,
                                              std::uint64_t usable,
                                              EvidenceClass klass = EvidenceClass::Measured) {
  CapacityObservation value;
  const auto id = ResourceId::parse(resource);
  value.resource = id.has_value() ? *id : ResourceId{};
  value.at = Timestamp::from_unix_nanos(cfn::test::test_now().unix_nanos -
                                        Duration::from_seconds(seconds_ago).nanos);
  value.observed_usable = Capacity::from_units(usable);
  value.evidence_class = klass;
  value.provenance = observed_provenance(cfn::test::test_source(), ProvenanceSource::TelemetryCollector,
                                         value.at, Duration::from_hours(48));
  return value;
}

[[nodiscard]] PredictionRequest base_request() {
  PredictionRequest request;
  request.kind = PredictionModelKind::BoundedLinearTrend;
  request.min_samples = 3;
  request.max_sample_age = Duration::from_hours(24);
  request.max_extrapolation = Duration::from_hours(24);
  request.max_growth_per_hour = Capacity::from_units(1000000000ULL);
  request.max_extrapolation_ppm = 500000;
  request.epoch = FabricEpoch::initial();
  request.horizon = Timestamp::from_unix_nanos(cfn::test::test_now().unix_nanos +
                                               Duration::from_seconds(600).nanos);
  return request;
}

}  // namespace

CFN_TEST(prediction, bounded_linear_trend_follows_the_observations) {
  PredictionRequest request = base_request();
  request.history = {observation("res", 300, 100), observation("res", 240, 200),
                     observation("res", 180, 300), observation("res", 120, 400),
                     observation("res", 60, 500)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK_EQ(static_cast<int>(result->evidence_class), static_cast<int>(EvidenceClass::Predicted));
  CFN_CHECK(!result->usable_as_observation());
  CFN_CHECK(result->predicted_available.units >= 500ULL);
  CFN_CHECK_EQ(result->base_observed.units, 500ULL);
  CFN_CHECK_EQ(result->per_resource.size(), 1U);
  CFN_CHECK_EQ(result->per_resource.front().sample_count, 5U);
}

CFN_TEST(prediction, insufficient_samples_yield_unknown) {
  PredictionRequest request = base_request();
  request.min_samples = 5;
  request.history = {observation("res", 60, 100), observation("res", 30, 200)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->available);
  CFN_CHECK(!result->unavailable_reason.empty());
  CFN_CHECK_EQ(result->predicted_available.units, 0ULL);
}

CFN_TEST(prediction, empty_history_yields_unknown) {
  PredictionRequest request = base_request();
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->available);
}

CFN_TEST(prediction, horizon_beyond_the_allowance_is_unknown) {
  PredictionRequest request = base_request();
  request.max_extrapolation = Duration::from_seconds(60);
  request.horizon = Timestamp::from_unix_nanos(cfn::test::test_now().unix_nanos +
                                               Duration::from_seconds(600).nanos);
  request.history = {observation("res", 300, 100), observation("res", 240, 200),
                     observation("res", 180, 300)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->available);
}

CFN_TEST(prediction, horizon_before_the_newest_sample_is_unknown) {
  PredictionRequest request = base_request();
  request.history = {observation("res", 300, 100), observation("res", 240, 200),
                     observation("res", 180, 300)};
  request.horizon = Timestamp::from_unix_nanos(cfn::test::test_now().unix_nanos -
                                               Duration::from_seconds(600).nanos);
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->available);
}

CFN_TEST(prediction, stale_samples_are_excluded) {
  PredictionRequest request = base_request();
  request.min_samples = 3;
  request.max_sample_age = Duration::from_seconds(120);
  request.history = {observation("res", 300, 100), observation("res", 240, 200),
                     observation("res", 60, 300)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->available);
}

CFN_TEST(prediction, future_dated_samples_are_excluded) {
  PredictionRequest request = base_request();
  CapacityObservation ahead = observation("res", 0, 900);
  ahead.at = Timestamp::from_unix_nanos(cfn::test::test_now().unix_nanos +
                                        Duration::from_seconds(60).nanos);
  request.history = {observation("res", 120, 100), observation("res", 60, 200), ahead};
  request.min_samples = 3;
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->available);
}

CFN_TEST(prediction, non_authoritative_samples_are_rejected) {
  PredictionRequest request = base_request();
  CapacityObservation derived = observation("res", 60, 500, EvidenceClass::Derived);
  derived.provenance.authoritative = false;
  request.history = {observation("res", 180, 100), observation("res", 120, 200), derived};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->available);
}

CFN_TEST(prediction, slope_is_clamped_to_the_declared_maximum) {
  PredictionRequest request = base_request();
  request.max_growth_per_hour = Capacity::from_units(10);
  request.max_extrapolation_ppm = 1000000U;
  request.max_extrapolation = Duration::from_hours(24);
  request.horizon = Timestamp::from_unix_nanos(cfn::test::test_now().unix_nanos +
                                               Duration::from_seconds(600).nanos);
  request.history = {observation("res", 300, 100), observation("res", 240, 1000),
                     observation("res", 180, 10000), observation("res", 120, 100000)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK(result->per_resource.front().slope_clamped);
  CFN_CHECK(result->per_resource.front().slope_per_hour <= 10);
  CFN_CHECK(result->confidence_ppm <= 500000U);
}

CFN_TEST(prediction, movement_is_bounded_by_the_allowance) {
  PredictionRequest request = base_request();
  request.max_extrapolation_ppm = 10000U;
  request.max_growth_per_hour = Capacity::from_units(1000000);
  request.history = {observation("res", 300, 100), observation("res", 240, 500),
                     observation("res", 180, 1000), observation("res", 120, 2000)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK(result->predicted_available.units <= 2000ULL + 20ULL);
  CFN_CHECK(result->per_resource.front().range_clamped);
}

CFN_TEST(prediction, last_value_repeats_the_newest_sample) {
  PredictionRequest request = base_request();
  request.kind = PredictionModelKind::LastValue;
  request.min_samples = 2;
  request.history = {observation("res", 120, 100), observation("res", 60, 250)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK_EQ(result->predicted_available.units, 250ULL);
  CFN_CHECK_EQ(result->per_resource.front().slope_per_hour, 0);
}

CFN_TEST(prediction, ewma_level_is_between_the_extremes) {
  PredictionRequest request = base_request();
  request.kind = PredictionModelKind::EwmaLevel;
  request.history = {observation("res", 180, 100), observation("res", 120, 400),
                     observation("res", 60, 500)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK(result->predicted_available.units >= 100ULL);
  CFN_CHECK(result->predicted_available.units <= 500ULL);
}

CFN_TEST(prediction, extreme_magnitudes_degrade_to_last_value) {
  PredictionRequest request = base_request();
  request.max_extrapolation_ppm = 1000000U;
  request.max_growth_per_hour = Capacity::from_units(1ULL << 62);
  request.max_extrapolation = Duration::from_hours(24);
  request.horizon = Timestamp::from_unix_nanos(cfn::test::test_now().unix_nanos +
                                               Duration::from_seconds(3600).nanos);
  const std::uint64_t huge = 1ULL << 61;
  request.history = {observation("res", 300, huge), observation("res", 240, huge - 1),
                     observation("res", 180, huge - 2), observation("res", 120, huge - 3)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK(result->predicted_available.units <= (1ULL << 62));
}

CFN_TEST(prediction, aggregate_over_several_resources) {
  PredictionRequest request = base_request();
  request.history = {observation("a", 180, 100), observation("a", 120, 100),
                     observation("a", 60, 100), observation("b", 180, 50),
                     observation("b", 120, 50), observation("b", 60, 50)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK_EQ(result->per_resource.size(), 2U);
  CFN_CHECK_EQ(result->predicted_available.units, 150ULL);
  CFN_CHECK_EQ(result->base_observed.units, 150ULL);
}

CFN_TEST(prediction, a_specific_resource_filters_the_history) {
  PredictionRequest request = base_request();
  request.resource = *ResourceId::parse("b");
  request.history = {observation("a", 180, 100), observation("a", 120, 100),
                     observation("a", 60, 100), observation("b", 180, 50),
                     observation("b", 120, 50), observation("b", 60, 50)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK_EQ(result->per_resource.size(), 1U);
  CFN_CHECK_EQ(result->predicted_available.units, 50ULL);
}

CFN_TEST(prediction, prediction_is_reproducible) {
  PredictionRequest request = base_request();
  request.history = {observation("res", 300, 100), observation("res", 240, 220),
                     observation("res", 180, 330), observation("res", 120, 450)};
  const auto first = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, first);
  const auto second = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, second);
  CFN_CHECK_EQ(first->predicted_available.units, second->predicted_available.units);
  CFN_CHECK_EQ(first->confidence_ppm, second->confidence_ppm);
  CFN_CHECK_EQ(first->per_resource.front().slope_per_hour,
               second->per_resource.front().slope_per_hour);
}

CFN_TEST(prediction, rejected_cases_are_stated_in_the_assumptions) {
  PredictionRequest request = base_request();
  request.history = {observation("res", 30, 100), observation("res", 20, 200),
                     observation("res", 10, 300)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(!result->assumptions.empty());
  bool mentions_model = false;
  for (const std::string& assumption : result->assumptions) {
    if (assumption.find("model=") != std::string::npos) {
      mentions_model = true;
    }
  }
  CFN_CHECK(mentions_model);
}

CFN_TEST(prediction, no_model_selected_is_rejected) {
  PredictionRequest request = base_request();
  request.kind = PredictionModelKind::None;
  CFN_REQUIRE_ERROR(cfn_ctx, predict(request, Limits(), cfn::test::test_now()),
                    ErrorCode::InvalidArgument);
}

CFN_TEST(prediction, sample_bound_truncates_the_history) {
  PredictionRequest request = base_request();
  for (int index = 0; index < 40; ++index) {
    request.history.push_back(observation("res", 1000 - (index * 10), 100 + static_cast<std::uint64_t>(index)));
  }
  Limits limits;
  limits.max_prediction_samples = 4;
  const auto result = predict(request, limits, cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->per_resource.front().sample_count <= 4U);
}

CFN_TEST(prediction, prediction_does_not_touch_an_observation_snapshot) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const std::uint64_t before_usable = snapshot->rollup.usable_total.units;
  const std::uint64_t before_available = snapshot->rollup.available_total.units;

  PredictionRequest request = base_request();
  request.history = {observation("res", 180, 100), observation("res", 120, 200),
                     observation("res", 60, 300)};
  const auto result = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, result);
  CFN_CHECK(result->available);
  CFN_CHECK_EQ(snapshot->rollup.usable_total.units, before_usable);
  CFN_CHECK_EQ(snapshot->rollup.available_total.units, before_available);
  CFN_CHECK(result->predicted_available.units != 0ULL);
}