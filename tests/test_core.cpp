// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <string>

#include "cfn/cfn.hpp"

using namespace cfn;
using namespace cfn::test;

CFN_TEST(core, identity_validation) {
  CFN_CHECK(ResourceId::parse("res-1").has_value());
  CFN_CHECK(ResourceId::parse("a").has_value());
  CFN_CHECK(!ResourceId::parse("").has_value());
  CFN_CHECK(!ResourceId::parse("has space").has_value());
  CFN_CHECK(!ResourceId::parse("tab\there").has_value());
  CFN_CHECK(!ResourceId::parse(std::string(65, 'x')).has_value());
  CFN_CHECK(ResourceId::parse(std::string(64, 'x')).has_value());
  CFN_CHECK(!ResourceId::parse("non-ascii-\xC3\xA9").has_value());

  const auto left = ResourceId::parse("alpha");
  const auto right = ResourceId::parse("beta");
  CFN_REQUIRE_OK(cfn_ctx, left.has_value() ? Outcome<void>() : Outcome<void>(Error(ErrorCode::Unknown, "x")));
  CFN_CHECK(*left != *right);
  CFN_CHECK(*left < *right);
  CFN_CHECK(left->hash() != right->hash());
}

CFN_TEST(core, identity_kinds_are_distinct_types) {
  const auto resource = ResourceId::parse("shared-name");
  const auto domain = FailureDomainId::parse("shared-name");
  CFN_CHECK(resource.has_value());
  CFN_CHECK(domain.has_value());
  CFN_CHECK(resource->view() == domain->view());
  CFN_CHECK(resource->hash() == domain->hash());
}

CFN_TEST(core, sequential_identity_is_stable) {
  const ResourceId first = ResourceId::sequential("node", 7);
  const ResourceId second = ResourceId::sequential("node", 7);
  const ResourceId third = ResourceId::sequential("node", 8);
  CFN_CHECK_EQ(first.view(), second.view());
  CFN_CHECK(first != third);
  CFN_CHECK(first < third);
  CFN_CHECK_EQ(first.view(), std::string_view("node-0000000000000007"));
}

CFN_TEST(core, checked_arithmetic) {
  std::uint64_t value = 0;
  CFN_CHECK(checked::add_u64(2, 3, value));
  CFN_CHECK_EQ(value, 3ULL + 2ULL);
  CFN_CHECK(!checked::add_u64(UINT64_MAX, 1, value));
  CFN_CHECK(checked::sub_u64(5, 3, value));
  CFN_CHECK_EQ(value, 2ULL);
  CFN_CHECK(!checked::sub_u64(1, 3, value));
  CFN_CHECK(checked::mul_u64(1ULL << 32, 1ULL << 31, value));
  CFN_CHECK_EQ(value, 1ULL << 63);
  CFN_CHECK(!checked::mul_u64(UINT64_MAX, 2, value));
  CFN_CHECK(checked::mul_div_u64(1000, 500000, 1000000, value));
  CFN_CHECK_EQ(value, 500ULL);
  CFN_CHECK(checked::scale_ppm(200, 250000, value));
  CFN_CHECK_EQ(value, 50ULL);
  CFN_CHECK(!checked::mul_div_u64(1, 1, 0, value));
}

CFN_TEST(core, checked_arithmetic_extremes) {
  std::uint64_t value = 0;
  CFN_CHECK(checked::mul_div_u64(UINT64_MAX, 1000000, 1000000, value));
  CFN_CHECK_EQ(value, UINT64_MAX);
  // The full product would overflow; the split computation must not.
  CFN_CHECK(checked::mul_div_u64(UINT64_MAX / 4, 999999, 1000000, value));
  CFN_CHECK(value < UINT64_MAX / 4);
  checked::Accumulator accumulator;
  CFN_CHECK(accumulator.add(UINT64_MAX));
  CFN_CHECK(!accumulator.add(1));
  CFN_CHECK(accumulator.overflowed());
  CFN_CHECK(!accumulator.valid());
}

CFN_TEST(core, bounded_string_rejects_and_truncates) {
  using Short = BoundedString<8>;
  const auto accepted = Short::from("12345678");
  CFN_CHECK(accepted.has_value());
  CFN_CHECK(!Short::from("123456789").has_value());
  const Short truncated = Short::truncated("123456789");
  CFN_CHECK_EQ(truncated.view(), std::string_view("12345678"));
  CFN_CHECK_EQ(truncated.size(), 8U);
  Short value;
  CFN_CHECK(value.assign("abc"));
  CFN_CHECK_EQ(value.c_str(), std::string("abc"));
  value.clear();
  CFN_CHECK(value.empty());
}

CFN_TEST(core, bounded_list_refuses_growth) {
  BoundedList<int> list(2);
  CFN_CHECK(list.try_push_back(1));
  CFN_CHECK(list.try_push_back(2));
  CFN_CHECK(!list.try_push_back(3));
  CFN_CHECK(list.at_capacity());
  CFN_CHECK_EQ(list.size(), 2U);
}

CFN_TEST(core, rng_is_reproducible) {
  Rng left(1234, 99);
  Rng right(1234, 99);
  for (int index = 0; index < 32; ++index) {
    CFN_CHECK_EQ(left.next_u64(), right.next_u64());
  }
  Rng other(1235, 99);
  CFN_CHECK(left.next_u64() != other.next_u64());
  Rng bounded(7, 1);
  for (int index = 0; index < 1000; ++index) {
    CFN_CHECK(bounded.bounded(10) < 10);
  }
  CFN_CHECK(!bounded.chance(0));
  CFN_CHECK(bounded.chance(1000000));
}

CFN_TEST(core, rng_shuffle_is_a_permutation) {
  Rng rng(42, 1);
  std::vector<std::uint32_t> values;
  for (std::uint32_t index = 0; index < 64; ++index) {
    values.push_back(index);
  }
  rng.shuffle(values);
  std::vector<std::uint32_t> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  for (std::uint32_t index = 0; index < 64; ++index) {
    CFN_CHECK_EQ(sorted[index], index);
  }
}

CFN_TEST(core, text_parsing_is_strict) {
  std::uint64_t value = 0;
  CFN_CHECK(text::parse_u64("0", value));
  CFN_CHECK_EQ(value, 0ULL);
  CFN_CHECK(text::parse_u64("18446744073709551615", value));
  CFN_CHECK_EQ(value, UINT64_MAX);
  CFN_CHECK(!text::parse_u64("18446744073709551616", value));
  CFN_CHECK(!text::parse_u64("", value));
  CFN_CHECK(!text::parse_u64("-1", value));
  CFN_CHECK(!text::parse_u64(" 1", value));
  CFN_CHECK(!text::parse_u64("1 ", value));
  CFN_CHECK(!text::parse_u64("1a", value));
  std::int64_t signed_value = 0;
  CFN_CHECK(text::parse_i64("-9223372036854775808", signed_value));
  CFN_CHECK_EQ(signed_value, INT64_MIN);
  CFN_CHECK(!text::parse_i64("9223372036854775808", signed_value));
  std::uint32_t ppm = 0;
  CFN_CHECK(text::parse_percent_ppm("12.5", ppm));
  CFN_CHECK_EQ(ppm, 125000U);
  CFN_CHECK(text::parse_percent_ppm("100%", ppm));
  CFN_CHECK_EQ(ppm, 1000000U);
  CFN_CHECK(!text::parse_percent_ppm("100.5", ppm));
  CFN_CHECK(!text::parse_percent_ppm("101", ppm));
  CFN_CHECK_EQ(text::format_ppm(125000), std::string("12.5%"));
  CFN_CHECK(text::parse_percent_ppm("0.0001", ppm));
  CFN_CHECK_EQ(ppm, 1U);
  CFN_CHECK(!text::parse_percent_ppm("0.00001", ppm));
}

CFN_TEST(core, time_helpers) {
  const Timestamp start = Timestamp::from_unix_seconds(10);
  const Timestamp later = Timestamp::from_unix_seconds(12);
  Duration age{};
  CFN_CHECK(elapsed(later, start, age));
  CFN_CHECK_EQ(age.nanos, Duration::from_seconds(2).nanos);
  CFN_CHECK(!elapsed(Timestamp::from_unix_nanos(INT64_MIN), Timestamp::from_unix_nanos(INT64_MAX), age));
  ManualClock clock(start, Instant{0});
  clock.advance(Duration::from_seconds(5));
  CFN_CHECK_EQ(clock.wall_now().unix_nanos, Timestamp::from_unix_seconds(15).unix_nanos);
  CFN_CHECK_EQ(clock.mono_now().nanos, Duration::from_seconds(5).nanos);
  CFN_CHECK_EQ(to_iso8601(Timestamp::from_unix_seconds(0)), std::string("1970-01-01T00:00:00.000000000Z"));
  CFN_CHECK_EQ(to_iso8601(Timestamp::from_unix_nanos(INT64_MAX)),
               std::string("2262-04-11T23:47:16.854775807Z"));
  CFN_CHECK_EQ(to_iso8601(Timestamp::from_unix_nanos(INT64_MIN)), std::string("1677-09-21T00:12:43.145224192Z"));
  // The seconds-based constructor saturates instead of overflowing.
  CFN_CHECK_EQ(Timestamp::from_unix_seconds(INT64_MAX).unix_nanos, INT64_MAX);
}

CFN_TEST(core, generation_and_epoch_semantics) {
  CFN_CHECK(!Generation{}.valid());
  CFN_CHECK(Generation::initial().valid());
  CFN_CHECK(Generation::from_value(2).is_newer_than(Generation::initial()));
  CFN_CHECK(!Generation::initial().is_newer_than(Generation::from_value(2)));
  const auto next = Generation::from_value(UINT64_MAX).try_next();
  CFN_CHECK(!next.has_value());
  CFN_CHECK_EQ(FabricEpoch::initial().value(), 1ULL);
}

CFN_TEST(core, provenance_rules) {
  Provenance unknown;
  CFN_CHECK(!is_fresh(unknown, test_now()));
  CFN_CHECK(!validate(unknown).has_value());

  Provenance prediction = predicted_provenance(test_source(), test_now(), FabricEpoch::initial());
  prediction.authoritative = true;
  CFN_REQUIRE_ERROR(cfn_ctx, validate(prediction), ErrorCode::NotAuthoritative);

  Provenance observed = observed_provenance(test_source(), ProvenanceSource::TelemetryCollector,
                                            test_now(), Duration::from_seconds(60));
  CFN_REQUIRE_OK(cfn_ctx, validate(observed));
  CFN_CHECK(is_fresh(observed, test_now()));
  CFN_CHECK(is_fresh(observed, Timestamp::from_unix_nanos(test_now().unix_nanos + Duration::from_seconds(59).nanos)));
  CFN_CHECK(!is_fresh(observed, Timestamp::from_unix_nanos(test_now().unix_nanos + Duration::from_seconds(61).nanos)));
  CFN_CHECK(!is_fresh(observed, Timestamp::from_unix_nanos(test_now().unix_nanos - Duration::from_seconds(1).nanos)));

  Provenance declared = declared_provenance(test_source(), test_now(), Duration{});
  CFN_CHECK(declared.authoritative);
  CFN_CHECK(is_fresh(declared, Timestamp::from_unix_seconds(999999999LL)));

  Provenance derived = derived_provenance(test_source(), test_now());
  CFN_CHECK(!derived.authoritative);
  CFN_CHECK(!is_fresh(derived, test_now()));
}

CFN_TEST(core, build_banner_reports_version) {
  const std::string banner = build_banner();
  CFN_CHECK(banner.find("1.0.0") != std::string::npos);
  CFN_CHECK_EQ(std::string(version_string), std::string("1.0.0"));
}

CFN_TEST(core, limits_validation) {
  Limits limits;
  CFN_REQUIRE_OK(cfn_ctx, limits.validate());
  Limits broken;
  broken.max_resources = 0;
  CFN_REQUIRE_ERROR(cfn_ctx, broken.validate(), ErrorCode::InvalidArgument);
  Limits oversized;
  oversized.max_capacity_units = 1ULL << 63;
  CFN_REQUIRE_ERROR(cfn_ctx, oversized.validate(), ErrorCode::InvalidArgument);
}

CFN_TEST(core, error_text_is_bounded) {
  const std::string huge(4096, 'x');
  const Error error(ErrorCode::Corrupt, huge);
  CFN_CHECK(error.message().size() <= kMaxErrorTextBytes);
  CFN_CHECK(error.to_text().size() < 4096);
}