// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;

namespace {

[[nodiscard]] std::uint64_t units(const Capacity& value) { return value.units; }

[[nodiscard]] const ResourceAccounting& row_of(const CapacitySnapshot& snapshot, const char* id) {
  const auto parsed = ResourceId::parse(id);
  const ResourceAccounting* row = parsed.has_value() ? snapshot.find(*parsed) : nullptr;
  static ResourceAccounting empty;
  return row == nullptr ? empty : *row;
}

}  // namespace

CFN_TEST(accounting, deductions_close_exactly) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 100, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 100, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 1000, "fd");
  fixture.add_reservation("res", "cap", 200);
  fixture.add_degradation("cap", 100000);
  fixture.policy.headroom_floor_ppm = 50000;
  fixture.has_shape = false;
  fixture.model.demand_shape = DemandShapeId{};
  fixture.model.demand_shape_generation = Generation{};
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");

  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->rollup.raw_total), 1200ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.degraded_total), 100ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.reserved_total), 200ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.headroom_total), 60ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.available_total), 840ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
  CFN_CHECK(snapshot->closure_verified);
}

CFN_TEST(accounting, unknown_capacity_is_never_available) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("known", 400, "fd");
  fixture.add_resource("unknown", 900, "fd", false);
  fixture.has_shape = false;
  fixture.model.demand_shape = DemandShapeId{};
  fixture.model.demand_shape_generation = Generation{};
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "known");
  fixture.add_edge("a", "b", "unknown");

  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->rollup.raw_total), 400ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.unknown_total), 900ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.available_total), 400ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.usable_total), 400ULL);
  const ResourceAccounting& unknown = row_of(*snapshot, "unknown");
  CFN_CHECK_EQ(units(unknown.available), 0ULL);
  CFN_CHECK(!unknown.authoritative);
}

CFN_TEST(accounting, unhealthy_failure_domain_zeroes_the_resource) {
  GraphFixture fixture;
  fixture.add_domain("fd-up", true);
  fixture.add_domain("fd-down", false);
  fixture.add_resource("a", 0, "fd-up", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd-up", true, ResourceKind::Node);
  fixture.add_resource("live", 500, "fd-up");
  fixture.add_resource("dead", 500, "fd-down");
  fixture.add_reservation("res", "dead", 100);
  fixture.has_shape = false;
  fixture.model.demand_shape = DemandShapeId{};
  fixture.model.demand_shape_generation = Generation{};
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "live");
  fixture.add_edge("a", "b", "dead");

  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->rollup.raw_total), 1000ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.degraded_total), 500ULL);
  // A lost domain subsumes its reservations: reserved plus degraded must never
  // exceed the raw capacity of the resource.
  CFN_CHECK_EQ(units(snapshot->rollup.reserved_total), 0ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.available_total), 500ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
  CFN_CHECK(!row_of(*snapshot, "dead").domain_healthy);
}

CFN_TEST(accounting, reservation_over_capacity_is_contradictory) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 100, "fd");
  fixture.add_reservation("too-much", "cap", 101);
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Contradictory);
}

CFN_TEST(accounting, headroom_and_reservation_together_are_contradictory) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 100, "fd");
  fixture.add_reservation("res", "cap", 80);
  fixture.policy.headroom_floor_ppm = 500000;
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Contradictory);
}

CFN_TEST(accounting, degradation_on_non_authoritative_resource_is_ignored) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("unknown", 900, "fd", false);
  fixture.add_degradation("unknown", 500000);
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "unknown");
  fixture.has_shape = false;
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->rollup.raw_total), 0ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.degraded_total), 0ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.unknown_total), 900ULL);
}

CFN_TEST(accounting, reject_unknown_policy_refuses_the_evaluation) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("unknown", 900, "fd", false);
  fixture.policy.reject_unknown_capacity = true;
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "unknown");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::NotAuthoritative);
}

CFN_TEST(accounting, capacity_total_overflow_is_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("left", 1ULL << 62, "fd");
  fixture.add_resource("right", 1ULL << 62, "fd");
  fixture.add_resource("third", 1ULL << 62, "fd");
  fixture.add_resource("fourth", 1ULL << 62, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "left");
  fixture.add_edge("a", "b", "right");
  fixture.add_edge("a", "b", "third");
  fixture.add_edge("a", "b", "fourth");
  fixture.has_shape = false;
  fixture.model.demand_shape = DemandShapeId{};
  fixture.model.demand_shape_generation = Generation{};
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Overflow);
}

CFN_TEST(accounting, closure_verifier_detects_a_broken_identity) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CapacitySnapshot tampered = *snapshot;
  tampered.rollup.available_total = Capacity::from_units(tampered.rollup.available_total.units + 1);
  CFN_REQUIRE_ERROR(cfn_ctx, verify_closure(tampered), ErrorCode::Contradictory);
}

CFN_TEST(accounting, rollup_counts_match_the_population) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->rollup.resource_count, 8U);
  CFN_CHECK_EQ(snapshot->rollup.present_resource_count, 8U);
  CFN_CHECK_EQ(snapshot->rollup.authoritative_resource_count, 8U);
  CFN_CHECK_EQ(snapshot->rollup.unknown_resource_count, 0U);
}