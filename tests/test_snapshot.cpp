// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <string>

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

CFN_TEST(snapshot, generation_vector_records_every_input) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const GenerationVector& generations = snapshot->generations;
  CFN_CHECK_EQ(generations.fabric_epoch.value(), 1ULL);
  CFN_CHECK_EQ(generations.model.value(), 1ULL);
  CFN_CHECK_EQ(generations.policy.value(), 1ULL);
  CFN_CHECK_EQ(generations.topology.value(), 1ULL);
  CFN_CHECK_EQ(generations.resource_catalog.value(), 1ULL);
  CFN_CHECK_EQ(generations.reservation_snapshot.value(), 1ULL);
  CFN_CHECK_EQ(generations.failure_domain_catalog.value(), 1ULL);
  CFN_CHECK_EQ(generations.demand_shape.value(), 1ULL);
  CFN_CHECK(generations.resource_set.valid());
  CFN_CHECK_EQ(generations.resource_count, 8U);
  CFN_CHECK_EQ(generations.computed_at.unix_nanos, cfn::test::test_now().unix_nanos);
}

CFN_TEST(snapshot, snapshot_identity_is_deterministic) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto first = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, first);
  const auto second = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, second);
  CFN_CHECK_EQ(first->id.view(), second->id.view());
  CFN_CHECK_EQ(generation_vector_digest(first->generations).value,
               generation_vector_digest(second->generations).value);

  GraphFixture other = cfn::test::diamond(101, 50, 80);
  const auto changed = other.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, changed);
  CFN_CHECK(first->id.view() != changed->id.view());
}

CFN_TEST(snapshot, confidence_summarises_evidence_completeness) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->confidence.evidence_completeness_ppm, 1000000U);
  CFN_CHECK(snapshot->confidence.all_present_resources_authoritative);
  CFN_CHECK(snapshot->confidence.exact_fit);
  CFN_CHECK_EQ(snapshot->confidence.unknown_resource_count, 0U);
}

CFN_TEST(snapshot, unknown_resources_reduce_the_completeness_figure) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.add_resource("unknown-1", 10, "fd-a", false);
  fixture.add_resource("unknown-2", 10, "fd-a", false);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->confidence.unknown_resource_count, 2U);
  CFN_CHECK_EQ(snapshot->confidence.present_resource_count, 10U);
  CFN_CHECK(snapshot->confidence.evidence_completeness_ppm < 1000000U);
  CFN_CHECK(!snapshot->confidence.all_present_resources_authoritative);
}

CFN_TEST(snapshot, unhealthy_domains_reduce_confidence) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.domains.domains.front().healthy = false;
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->confidence.unhealthy_domain_count, 1U);
  CFN_CHECK(snapshot->confidence.evidence_completeness_ppm < 1000000U);
}

CFN_TEST(snapshot, resource_lookup_finds_and_misses) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK(snapshot->find(*ResourceId::parse("cap-sa")) != nullptr);
  CFN_CHECK(snapshot->find(*ResourceId::parse("nope")) == nullptr);
}

CFN_TEST(snapshot, evaluator_rejects_missing_inputs) {
  EvaluationInputs inputs;
  EvaluationOptions options;
  options.now = cfn::test::test_now();
  options.epoch = FabricEpoch::initial();
  options.provenance = cfn::test::test_provenance();
  CFN_REQUIRE_ERROR(cfn_ctx, evaluate(inputs, options), ErrorCode::InvalidArgument);
}

CFN_TEST(snapshot, evaluator_rejects_a_policy_that_the_model_does_not_name) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto other = PolicyId::parse("other-policy");
  CFN_CHECK(other.has_value());
  fixture.model.policy = *other;
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::NotFound);
}

CFN_TEST(snapshot, evaluator_rejects_a_policy_generation_mismatch) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.model.policy_generation = Generation::from_value(7);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::StalePolicy);
}

CFN_TEST(snapshot, evaluator_rejects_a_demand_shape_generation_mismatch) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.model.demand_shape_generation = Generation::from_value(9);
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::StaleDemandShape);
}

CFN_TEST(snapshot, evaluator_rejects_a_surplus_demand_shape) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.model.demand_shape = DemandShapeId{};
  fixture.model.demand_shape_generation = Generation{};
  CFN_REQUIRE_ERROR(cfn_ctx, fixture.evaluate(), ErrorCode::Contradictory);
}

CFN_TEST(snapshot, cancellation_produces_no_snapshot) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  EvaluationOptions options;
  options.limits = Limits();
  options.now = cfn::test::test_now();
  options.epoch = FabricEpoch::initial();
  options.provenance = cfn::test::test_provenance();
  options.cancel.cancel();
  CFN_REQUIRE_ERROR(cfn_ctx, evaluate(fixture.inputs(), options), ErrorCode::Cancelled);
}

CFN_TEST(snapshot, explicit_snapshot_identity_is_honoured) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  EvaluationOptions options;
  options.now = cfn::test::test_now();
  options.epoch = FabricEpoch::initial();
  options.provenance = cfn::test::test_provenance();
  options.snapshot_generation = Generation::from_value(11);
  const auto id = CapacitySnapshotId::parse("explicit-snapshot");
  CFN_CHECK(id.has_value());
  options.snapshot_id = *id;
  const auto snapshot = evaluate(fixture.inputs(), options);
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  CFN_CHECK_EQ(snapshot->id.view(), id->view());
  CFN_CHECK_EQ(snapshot->generation.value(), 11ULL);
}