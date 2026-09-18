// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <string>

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

const char* kScenario = R"(# synthetic scenario
version 1
name demo
fabric-epoch 3
domain fd-a healthy=true
domain fd-b healthy=false
resource src kind=node capacity=0 fd=fd-a
resource dst kind=node capacity=0 fd=fd-b
resource cap-1 kind=link capacity=100 fd=fd-a
resource cap-2 kind=link capacity=60 fd=fd-b
node src
node dst
edge e-1 src dst cap-1
edge e-2 src dst cap-2
reservation r-1 cap-1 class=committed amount=20
degrade cap-2 cause=health ppm=100000
policy p-1 headroom-ppm=50000 resilience=none
shape s-1 granularity=0
flow f-1 src dst magnitude=40
model m-1 policy=p-1 shape=s-1 label=demo
)";

[[nodiscard]] Outcome<text::Scenario> parse(const char* value) {
  return text::parse_scenario(value, Limits(), cfn::test::test_now());
}

}  // namespace

CFN_TEST(scenario, parses_a_complete_scenario) {
  const auto scenario = parse(kScenario);
  CFN_REQUIRE_OK(cfn_ctx, scenario);
  CFN_CHECK_EQ(scenario->name, std::string("demo"));
  CFN_CHECK_EQ(scenario->epoch.value(), 3ULL);
  CFN_CHECK_EQ(scenario->resources.resources.size(), 4U);
  CFN_CHECK_EQ(scenario->topology.nodes.size(), 2U);
  CFN_CHECK_EQ(scenario->topology.edges.size(), 2U);
  CFN_CHECK_EQ(scenario->domains.domains.size(), 2U);
  CFN_CHECK_EQ(scenario->reservations.reservations.size(), 1U);
  CFN_CHECK_EQ(scenario->degradation.records.size(), 1U);
  CFN_CHECK_EQ(scenario->demand_shape.flows.size(), 1U);
  CFN_CHECK_EQ(scenario->policy.headroom_floor_ppm, 50000U);
  CFN_CHECK(scenario->has_demand_shape);
  CFN_CHECK_EQ(scenario->model.policy.view(), std::string_view("p-1"));
  CFN_CHECK_EQ(scenario->domains.domains[1].healthy, false);
}

CFN_TEST(scenario, parsed_scenario_evaluates_to_the_expected_accounting) {
  const auto scenario = parse(kScenario);
  CFN_REQUIRE_OK(cfn_ctx, scenario);
  GraphFixture fixture;
  fixture.resources = scenario->resources;
  fixture.topology = scenario->topology;
  fixture.domains = scenario->domains;
  fixture.reservations = scenario->reservations;
  fixture.degradation = scenario->degradation;
  fixture.policy = scenario->policy;
  fixture.shape = scenario->demand_shape;
  fixture.model = scenario->model;
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->rollup.raw_total.units, 160ULL);
  CFN_CHECK_EQ(snapshot->rollup.reserved_total.units, 20ULL);
  // cap-1 sits in the healthy domain: 5% protected headroom on 100 units.
  // cap-2 sits in the lost domain, so its whole 60 units are degraded and its
  // reservations and headroom are subsumed rather than double counted.
  CFN_CHECK_EQ(snapshot->rollup.headroom_total.units, 5ULL);
  CFN_CHECK_EQ(snapshot->rollup.degraded_total.units, 60ULL);
  CFN_CHECK_EQ(snapshot->rollup.available_total.units, 75ULL);
  CFN_CHECK_EQ(snapshot->rollup.usable_total.units, 40ULL);
  CFN_CHECK_EQ(snapshot->rollup.stranded_total.units, 0ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(scenario, round_trip_is_stable) {
  const auto scenario = parse(kScenario);
  CFN_REQUIRE_OK(cfn_ctx, scenario);
  const std::string first = text::format_scenario(*scenario);
  const auto reparsed = parse(first.c_str());
  CFN_REQUIRE_OK(cfn_ctx, reparsed);
  const std::string second = text::format_scenario(*reparsed);
  CFN_CHECK_EQ(first, second);
}

CFN_TEST(scenario, missing_version_is_rejected) {
  CFN_REQUIRE_ERROR(cfn_ctx, parse("resource a capacity=1\n"), ErrorCode::MalformedInput);
}

CFN_TEST(scenario, unsupported_version_is_rejected) {
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 2\n"), ErrorCode::MalformedInput);
}

CFN_TEST(scenario, unknown_directive_is_rejected) {
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nnonsense a b\n"), ErrorCode::MalformedInput);
}

CFN_TEST(scenario, malformed_number_is_rejected) {
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nresource a capacity=abc\n"), ErrorCode::MalformedInput);
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nresource a capacity=-1\n"), ErrorCode::MalformedInput);
  CFN_REQUIRE_ERROR(cfn_ctx,
                    parse("version 1\nresource a capacity=99999999999999999999999\n"),
                    ErrorCode::MalformedInput);
}

CFN_TEST(scenario, missing_required_field_is_rejected) {
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nresource a kind=link\n"), ErrorCode::MalformedInput);
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nedge e a b c\n"), ErrorCode::MalformedInput);
}

CFN_TEST(scenario, duplicate_resource_is_rejected) {
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nresource a capacity=1\nresource a capacity=2\n"),
                    ErrorCode::MalformedInput);
}

CFN_TEST(scenario, forward_reference_is_rejected) {
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nnode a\nresource a capacity=1\n"),
                    ErrorCode::MalformedInput);
  CFN_REQUIRE_ERROR(cfn_ctx, parse("version 1\nedge e a b c\nresource a capacity=1\n"),
                    ErrorCode::MalformedInput);
}

CFN_TEST(scenario, reservation_on_an_unknown_resource_is_rejected) {
  const char* text_value =
      "version 1\n"
      "resource a kind=node capacity=0\n"
      "resource b kind=node capacity=0\n"
      "resource cap kind=link capacity=10\n"
      "node a\nnode b\nedge e a b cap\n"
      "reservation r missing amount=1\n";
  CFN_REQUIRE_ERROR(cfn_ctx, parse(text_value), ErrorCode::MalformedInput);
}

CFN_TEST(scenario, reservation_beyond_capacity_is_rejected) {
  const char* text_value =
      "version 1\n"
      "resource a kind=node capacity=0\n"
      "resource b kind=node capacity=0\n"
      "resource cap kind=link capacity=10\n"
      "node a\nnode b\nedge e a b cap\n"
      "reservation r cap amount=11\n";
  CFN_REQUIRE_ERROR(cfn_ctx, parse(text_value), ErrorCode::Contradictory);
}

CFN_TEST(scenario, stale_resource_generation_binding_is_rejected) {
  const char* text_value =
      "version 1\n"
      "resource a kind=node capacity=0 generation=2\n"
      "resource b kind=node capacity=0\n"
      "resource cap kind=link capacity=10 generation=5\n"
      "node a\nnode b\nedge e a b cap capacity-generation=4\n";
  CFN_REQUIRE_ERROR(cfn_ctx, parse(text_value), ErrorCode::StaleResource);
}

CFN_TEST(scenario, degradation_needs_exactly_one_loss_form) {
  const char* base =
      "version 1\n"
      "resource a kind=node capacity=0\n"
      "resource b kind=node capacity=0\n"
      "resource cap kind=link capacity=10\n"
      "node a\nnode b\nedge e a b cap\n";
  CFN_REQUIRE_ERROR(cfn_ctx, parse((std::string(base) + "degrade cap\n").c_str()),
                    ErrorCode::MalformedInput);
  CFN_REQUIRE_ERROR(cfn_ctx, parse((std::string(base) + "degrade cap lost=1 ppm=1\n").c_str()),
                    ErrorCode::MalformedInput);
  CFN_REQUIRE_ERROR(cfn_ctx, parse((std::string(base) + "degrade cap ppm=2000000\n").c_str()),
                    ErrorCode::InvalidArgument);
}

CFN_TEST(scenario, population_limits_are_enforced) {
  Limits limits;
  limits.max_resources = 1;
  CFN_REQUIRE_ERROR(cfn_ctx,
                    text::parse_scenario("version 1\nresource a capacity=1\nresource b capacity=1\n",
                                         limits, cfn::test::test_now()),
                    ErrorCode::LimitExceeded);
}

CFN_TEST(scenario, oversized_tokens_are_rejected) {
  const std::string huge(500, 'x');
  CFN_REQUIRE_ERROR(cfn_ctx, parse(("version 1\nresource " + huge + " capacity=1\n").c_str()),
                    ErrorCode::MalformedInput);
}

CFN_TEST(scenario, comments_and_blank_lines_are_ignored) {
  const auto scenario = parse("\n# comment\nversion 1\n\n# another\n");
  CFN_REQUIRE_OK(cfn_ctx, scenario);
  CFN_CHECK(scenario->resources.resources.empty());
}

CFN_TEST(scenario, missing_file_is_reported) {
  const auto scenario = text::load_scenario("does-not-exist.cfnscene", Limits(), cfn::test::test_now());
  CFN_REQUIRE_ERROR(cfn_ctx, scenario, ErrorCode::NotFound);
}