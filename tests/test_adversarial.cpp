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

CFN_TEST(adversarial, capacity_beyond_the_limit_is_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", (1ULL << 62) + 1, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::LimitExceeded);
}

CFN_TEST(adversarial, duplicate_resource_identities_are_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("dup", 10, "fd");
  fixture.add_resource("dup", 20, "fd");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Duplicate);
}

CFN_TEST(adversarial, duplicate_topology_edge_identities_are_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 10, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");
  fixture.topology.edges.push_back(fixture.topology.edges.front());
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Duplicate);
}

CFN_TEST(adversarial, edge_with_an_unknown_endpoint_is_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 10, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "ghost", "cap");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::NotFound);
}

CFN_TEST(adversarial, edge_bound_to_a_stale_resource_generation_is_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 10, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");
  fixture.topology.edges.front().capacity_resource_generation = Generation::from_value(9);
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::StaleResource);
}

CFN_TEST(adversarial, degradation_on_an_unknown_resource_is_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 10, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");
  fixture.add_degradation("ghost", 1000);
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::NotFound);
}

CFN_TEST(adversarial, resources_in_an_undeclared_failure_domain_are_rejected) {
  GraphFixture fixture;
  fixture.add_resource("a", 0, "fd-missing", true, ResourceKind::Node);
  fixture.add_node("a");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::NotFound);
}

CFN_TEST(adversarial, duplicate_failure_domain_identities_are_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_node("a");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Duplicate);
}

CFN_TEST(adversarial, duplicate_flow_identities_are_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.shape.flows.push_back(fixture.shape.flows.front());
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Duplicate);
}

CFN_TEST(adversarial, identical_flow_endpoints_are_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.shape.flows.front().sink = fixture.shape.flows.front().source;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::InvalidArgument);
}

CFN_TEST(adversarial, a_shape_without_flows_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.shape.flows.clear();
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::MalformedInput);
}

CFN_TEST(adversarial, oversized_flow_population_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  for (int index = 0; index < 32; ++index) {
    fixture.add_flow(("flow-" + std::to_string(index + 2)).c_str(), "src", "dst", 1);
  }
  Limits limits;
  limits.max_demand_flows = 8;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(limits), ErrorCode::LimitExceeded);
}

CFN_TEST(adversarial, oversized_resource_population_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  Limits limits;
  limits.max_resources = 2;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(limits), ErrorCode::LimitExceeded);
}

CFN_TEST(adversarial, degradation_fraction_above_one_hundred_percent_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.degradation.records.clear();
  fixture.add_degradation("cap-sa", 1000001);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::InvalidArgument);
}

CFN_TEST(adversarial, degradation_with_both_loss_forms_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.degradation.records.clear();
  fixture.add_degradation("cap-sa", 1000, 5);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::MalformedInput);
}

CFN_TEST(adversarial, zero_amount_reservation_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.add_reservation("zero", "cap-sa", 0);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::InvalidArgument);
}

CFN_TEST(adversarial, duplicate_reservation_identities_are_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.add_reservation("dup", "cap-sa", 5);
  fixture.add_reservation("dup", "cap-sb", 5);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Duplicate);
}

CFN_TEST(adversarial, reservation_on_a_withdrawn_resource_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.add_reservation("res", "cap-sa", 5);
  // Index 4 in the diamond is the cap-sa capacity resource.
  fixture.resources.resources[4].present = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Contradictory);
}

CFN_TEST(adversarial, invalid_limb_limits_are_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  Limits limits;
  limits.max_frame_bytes = 1;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(limits), ErrorCode::InvalidArgument);
}

CFN_TEST(adversarial, oversized_policy_headroom_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.policy.headroom_floor_ppm = 1000001;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::InvalidArgument);
  fixture.policy.headroom_floor_ppm = 0;
  fixture.policy.headroom_floor_absolute = Capacity::from_units(1ULL << 62);
  Limits limits;
  limits.max_capacity_units = 100;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(limits), ErrorCode::LimitExceeded);
}

CFN_TEST(adversarial, self_loop_edges_do_not_create_capacity) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("loop", 100, "fd");
  fixture.add_resource("cap", 50, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "a", "loop");
  fixture.add_edge("a", "b", "cap");
  fixture.add_flow("flow-1", "a", "b", 200);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  // The loop is topologically on an admissible path, but every path still has
  // to leave through the single 50 unit arc, so the loop capacity is stranded
  // at that bottleneck rather than being advertised as usable.
  CFN_CHECK_EQ(snapshot->fragmentation.deliverable.units, 50ULL);
  CFN_CHECK_EQ(snapshot->fragmentation.stranding.segmentation.units, 0ULL);
  CFN_CHECK_EQ(snapshot->fragmentation.stranding.bottleneck.units, 100ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(adversarial, unreachable_sink_strands_everything) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("c", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 100, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_node("c");
  fixture.add_edge("a", "b", "cap");
  fixture.add_flow("flow-1", "a", "c", 10);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->fragmentation.deliverable.units, 0ULL);
  CFN_CHECK_EQ(snapshot->fragmentation.stranding.segmentation.units, 100ULL);
  CFN_CHECK_EQ(snapshot->rollup.usable_total.units, 0ULL);
}

CFN_TEST(adversarial, empty_topology_yields_zero_deliverable) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 100, "fd");
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_flow("flow-1", "a", "b", 10);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->fragmentation.deliverable.units, 0ULL);
  CFN_CHECK_EQ(snapshot->fragmentation.stranding.segmentation.units, 100ULL);
}

CFN_TEST(adversarial, withdrawn_resource_contributes_nothing) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.resources.resources[4].present = false;
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const ResourceAccounting* row = snapshot->find(*ResourceId::parse("cap-sa"));
  CFN_CHECK(row != nullptr);
  if (row != nullptr) {
    CFN_CHECK_EQ(row->raw.units, 0ULL);
    CFN_CHECK_EQ(row->available.units, 0ULL);
  }
}

CFN_TEST(adversarial, resource_granularity_beyond_capacity_is_rejected) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("a", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("b", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("cap", 10, "fd", true, ResourceKind::Link, 11);
  fixture.add_node("a");
  fixture.add_node("b");
  fixture.add_edge("a", "b", "cap");
  fixture.has_shape = false;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Contradictory);
}

CFN_TEST(adversarial, topology_population_bounds_are_enforced) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  Limits limits;
  limits.max_topology_edges = 2;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(limits), ErrorCode::LimitExceeded);
  Limits node_limits;
  node_limits.max_topology_nodes = 1;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(node_limits), ErrorCode::LimitExceeded);
}

CFN_TEST(adversarial, a_capacity_resource_cannot_back_two_arcs) {
  // Sharing one capacity resource between two arcs would turn the resource into
  // a hub that lets flow leave along a different arc from the one it entered,
  // which the authoritative topology does not declare. It is rejected instead
  // of being silently modelled.
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("src", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("mid", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("dst", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("shared", 100, "fd");
  fixture.add_node("src");
  fixture.add_node("mid");
  fixture.add_node("dst");
  fixture.add_edge("src", "mid", "shared");
  fixture.add_edge("mid", "dst", "shared");
  fixture.add_flow("flow-1", "src", "dst", 10);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Contradictory);
}

CFN_TEST(adversarial, a_capacity_resource_cannot_back_an_edge_and_a_transit) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("src", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("mid", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("dst", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("shared", 100, "fd");
  fixture.add_node("src");
  fixture.add_node("mid", "shared");
  fixture.add_node("dst");
  fixture.add_edge("src", "mid", "shared");
  fixture.add_edge("mid", "dst", "shared");
  fixture.add_flow("flow-1", "src", "dst", 10);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Contradictory);
}

CFN_TEST(adversarial, zero_multiplicity_edge_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.topology.edges.front().multiplicity = 0;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::InvalidArgument);
}

CFN_TEST(adversarial, unestablished_generation_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.resources.resources.front().generation = Generation{};
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::InvalidArgument);
}

CFN_TEST(adversarial, authoritative_unknown_evidence_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.resources.resources[4].capacity_evidence = EvidenceClass::Unknown;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::NotAuthoritative);
}