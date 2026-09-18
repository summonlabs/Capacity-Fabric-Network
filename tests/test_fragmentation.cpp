// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

[[nodiscard]] std::uint64_t units(const Capacity& value) { return value.units; }

}  // namespace

CFN_TEST(fragmentation, diamond_is_exact_for_a_single_flow) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const FragmentationResult& result = snapshot->fragmentation;
  CFN_CHECK(result.evaluated);
  CFN_CHECK(result.exact);
  CFN_CHECK_EQ(units(result.deliverable), 150ULL);
  CFN_CHECK_EQ(units(result.satisfied), 80ULL);
  CFN_CHECK_EQ(units(result.spare), 70ULL);
  CFN_CHECK_EQ(units(result.stranding.bottleneck), 150ULL);
  CFN_CHECK_EQ(units(result.stranding.segmentation), 0ULL);
  CFN_CHECK_EQ(units(result.stranding.failure_domain_resilience), 0ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.usable_total), 80ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.spare_total), 70ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.stranded_total), 150ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(fragmentation, disconnected_resource_is_segmented) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.add_resource("orphan", 70, "fd-a");
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->rollup.available_total), 370ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.deliverable), 150ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.stranding.segmentation), 70ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.stranding.bottleneck), 150ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.stranded_total), 220ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
  CFN_CHECK(!snapshot->fragmentation.segmentation_resources.empty());
  const auto orphan = ResourceId::parse("orphan");
  CFN_CHECK(orphan.has_value());
  const ResourceAccounting* row = orphan.has_value() ? snapshot->find(*orphan) : nullptr;
  CFN_CHECK(row != nullptr);
  if (row != nullptr) {
    CFN_CHECK(!row->admissible);
    CFN_CHECK_EQ(static_cast<int>(row->strand_cause), static_cast<int>(StrandCause::Segmentation));
    CFN_CHECK_EQ(units(row->stranded), 70ULL);
  }
}

CFN_TEST(fragmentation, narrow_section_becomes_the_bottleneck) {
  GraphFixture fixture;
  fixture.add_domain("fd-a");
  fixture.add_domain("fd-b");
  fixture.add_resource("src", 0, "fd-a", true, ResourceKind::Node);
  fixture.add_resource("dst", 0, "fd-b", true, ResourceKind::Node);
  fixture.add_resource("mid", 0, "fd-a", true, ResourceKind::Node);
  fixture.add_resource("wide", 100, "fd-a");
  fixture.add_resource("narrow", 10, "fd-a");
  fixture.add_node("src");
  fixture.add_node("dst");
  fixture.add_node("mid");
  fixture.add_edge("src", "mid", "wide");
  fixture.add_edge("mid", "dst", "narrow");
  fixture.add_flow("flow-1", "src", "dst", 40);

  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->rollup.available_total), 110ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.deliverable), 10ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.satisfied), 10ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.stranding.bottleneck), 100ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.stranding.segmentation), 0ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.stranded_total), 100ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(fragmentation, resilience_requirement_reduces_the_envelope) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.policy.resilience = ResilienceMode::DomainN1;
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK(snapshot->fragmentation.resilience_applied);
  CFN_CHECK_EQ(snapshot->fragmentation.resilience_scenarios, 2U);
  CFN_CHECK_EQ(units(snapshot->fragmentation.deliverable_unresilient), 150ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.deliverable), 50ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.stranding.failure_domain_resilience), 100ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.satisfied), 50ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.spare), 0ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.stranded_total), 250ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(fragmentation, n1n1_needs_two_domain_losses) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.add_domain("fd-c");
  fixture.policy.resilience = ResilienceMode::DomainN1N1;
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK(snapshot->fragmentation.resilience_applied);
  CFN_CHECK_EQ(snapshot->fragmentation.resilience_scenarios, 1U);
  CFN_CHECK_EQ(units(snapshot->fragmentation.deliverable), 0ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(fragmentation, n1n1_scenario_budget_is_bounded) {
  GraphFixture fixture;
  fixture.add_domain("fd-a");
  fixture.add_domain("fd-b");
  fixture.add_domain("fd-c");
  fixture.add_domain("fd-d");
  fixture.add_resource("src", 0, "fd-a", true, ResourceKind::Node);
  fixture.add_resource("dst", 0, "fd-b", true, ResourceKind::Node);
  fixture.add_node("src");
  fixture.add_node("dst");
  for (int index = 0; index < 4; ++index) {
    const std::string capacity = "cap-" + std::to_string(index);
    const std::string domain = "fd-" + std::string(1, static_cast<char>('a' + index));
    fixture.add_resource(capacity.c_str(), 100, domain.c_str());
    fixture.add_edge("src", "dst", capacity.c_str());
  }
  fixture.add_flow("flow-1", "src", "dst", 10);
  fixture.policy.resilience = ResilienceMode::DomainN1N1;
  Limits limits;
  limits.max_resilience_domains_scanned = 4;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(limits), ErrorCode::LimitExceeded);
  // Two domains give one scenario, which fits the budget.
  GraphFixture small;
  small.add_domain("fd-a");
  small.add_domain("fd-b");
  small.add_resource("src", 0, "fd-a", true, ResourceKind::Node);
  small.add_resource("dst", 0, "fd-b", true, ResourceKind::Node);
  small.add_resource("cap-a", 100, "fd-a");
  small.add_resource("cap-b", 100, "fd-b");
  small.add_node("src");
  small.add_node("dst");
  small.add_edge("src", "dst", "cap-a");
  small.add_edge("src", "dst", "cap-b");
  small.add_flow("flow-1", "src", "dst", 10);
  small.policy.resilience = ResilienceMode::DomainN1N1;
  const auto snapshot = small.evaluate(limits);
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->fragmentation.resilience_scenarios, 1U);
}

CFN_TEST(fragmentation, granularity_quantizes_the_admitted_demand) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 85);
  fixture.shape.granularity = Capacity::from_units(10);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->fragmentation.satisfied), 80ULL);
  const FlowFit& flow = snapshot->fragmentation.flows.front();
  CFN_CHECK_EQ(units(flow.admitted), 80ULL);
  CFN_CHECK_EQ(units(flow.unmet), 5ULL);
  CFN_CHECK(!flow.satisfied);
  CFN_CHECK_EQ(units(snapshot->fragmentation.granularity_loss), 0ULL);
}

CFN_TEST(fragmentation, granularity_loss_is_reported_when_the_fit_is_uneven) {
  GraphFixture fixture = cfn::test::diamond(45, 0, 80);
  fixture.shape.granularity = Capacity::from_units(10);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->fragmentation.satisfied), 40ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.granularity_loss), 5ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(fragmentation, multi_flow_reports_an_interval) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.add_flow("flow-2", "src", "dst", 40);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK(!snapshot->fragmentation.exact);
  CFN_CHECK_EQ(units(snapshot->fragmentation.deliverable), 150ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.satisfied), 80ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.satisfied_upper), 80ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(fragmentation, flow_beyond_a_topology_node_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.add_resource("lonely", 10, "fd-a");
  fixture.add_flow("flow-2", "lonely", "dst", 10);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::NotFound);
}

CFN_TEST(fragmentation, fit_query_answers_exactly) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  std::vector<ResourceAccounting> rows = snapshot->per_resource;
  FitQuery query;
  query.source = *ResourceId::parse("src");
  query.sink = *ResourceId::parse("dst");
  query.magnitude = Capacity::from_units(40);
  const auto answer = fit_query(rows, fixture.topology, query, Limits());
  CFN_REQUIRE_OK(cfn_ctx, answer);
  CFN_CHECK(answer->satisfiable);
  CFN_CHECK(answer->exact);
  CFN_CHECK_EQ(units(answer->admitted), 40ULL);
  CFN_CHECK_EQ(units(answer->deliverable), 150ULL);
  CFN_CHECK_EQ(units(answer->bottleneck_capacity), 150ULL);
  CFN_CHECK_EQ(units(answer->stranding.bottleneck), 150ULL);
}

CFN_TEST(fragmentation, fit_query_rejects_degenerate_endpoints) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  std::vector<ResourceAccounting> rows = snapshot->per_resource;
  FitQuery query;
  query.source = *ResourceId::parse("src");
  query.sink = query.source;
  CFN_REQUIRE_ERROR(cfn_ctx, fit_query(rows, fixture.topology, query, Limits()),
                    ErrorCode::InvalidArgument);
}

CFN_TEST(fragmentation, no_shape_leaves_fragmentation_unevaluated) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  fixture.has_shape = false;
  fixture.model.demand_shape = DemandShapeId{};
  fixture.model.demand_shape_generation = Generation{};
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK(!snapshot->fragmentation.evaluated);
  CFN_CHECK_EQ(units(snapshot->rollup.usable_total), 300ULL);
  CFN_CHECK_EQ(units(snapshot->rollup.stranded_total), 0ULL);
  CFN_REQUIRE_OK(cfn_ctx, verify_closure(*snapshot));
}

CFN_TEST(fragmentation, per_resource_witnesses_cover_the_segmented_total) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.add_resource("orphan", 70, "fd-a");
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  std::uint64_t segmented = 0;
  for (const ResourceAccounting& row : snapshot->per_resource) {
    if (row.strand_cause == StrandCause::Segmentation) {
      segmented += row.stranded.units;
    }
  }
  CFN_CHECK_EQ(segmented, units(snapshot->fragmentation.stranding.segmentation));
}

CFN_TEST(fragmentation, node_transit_capacity_is_charged) {
  GraphFixture fixture;
  fixture.add_domain("fd");
  fixture.add_resource("src", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("dst", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("mid", 0, "fd", true, ResourceKind::Node);
  fixture.add_resource("wide-a", 100, "fd");
  fixture.add_resource("wide-b", 100, "fd");
  fixture.add_resource("transit", 7, "fd");
  fixture.add_node("src");
  fixture.add_node("dst");
  fixture.add_node("mid", "transit");
  fixture.add_edge("src", "mid", "wide-a");
  fixture.add_edge("mid", "dst", "wide-b");
  fixture.add_flow("flow-1", "src", "dst", 50);

  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(units(snapshot->fragmentation.deliverable), 7ULL);
  CFN_CHECK_EQ(units(snapshot->fragmentation.satisfied), 7ULL);
}