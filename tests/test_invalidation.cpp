// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

[[nodiscard]] GenerationVector current_of(const CapacitySnapshot& snapshot) {
  return snapshot.generations;
}

}  // namespace

CFN_TEST(invalidation, unchanged_generations_stay_current) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const GenerationVector current = current_of(*snapshot);
  CFN_CHECK_EQ(static_cast<int>(classify_staleness(*snapshot, current)),
               static_cast<int>(StalenessReason::None));
  CFN_REQUIRE_OK(cfn_ctx, validate_binding(*snapshot, current));
}

CFN_TEST(invalidation, every_bound_generation_is_individually_enforced) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);

  {
    GenerationVector current = current_of(*snapshot);
    current.fabric_epoch = FabricEpoch::from_value(2);
    CFN_CHECK_EQ(static_cast<int>(classify_staleness(*snapshot, current)),
                 static_cast<int>(StalenessReason::EpochAdvanced));
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::EpochMismatch);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.model = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleModel);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.policy = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StalePolicy);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.topology = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleTopology);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.resource_catalog = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleResource);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.resource_set = Digest{0x1234};
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleResource);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.resource_count += 1U;
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleResource);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.reservation_snapshot = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleReservation);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.failure_domain_catalog = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleFailureDomain);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.degradation = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleEvidence);
  }
  {
    GenerationVector current = current_of(*snapshot);
    current.demand_shape = Generation::from_value(2);
    CFN_REQUIRE_ERROR(cfn_ctx, validate_binding(*snapshot, current), ErrorCode::StaleDemandShape);
  }
}

CFN_TEST(invalidation, classification_precedence_is_fixed) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  GenerationVector current = current_of(*snapshot);
  current.fabric_epoch = FabricEpoch::from_value(5);
  current.topology = Generation::from_value(5);
  current.policy = Generation::from_value(5);
  CFN_CHECK_EQ(static_cast<int>(classify_staleness(*snapshot, current)),
               static_cast<int>(StalenessReason::EpochAdvanced));
}

CFN_TEST(invalidation, reason_names_are_stable) {
  CFN_CHECK_EQ(to_string(StalenessReason::None), std::string_view("current"));
  CFN_CHECK_EQ(to_string(StalenessReason::EpochAdvanced), std::string_view("fabric-epoch-advanced"));
  CFN_CHECK_EQ(to_string(StalenessReason::ResourceSetChanged),
               std::string_view("resource-set-changed"));
}

CFN_TEST(invalidation, generation_digest_changes_with_any_bound_generation) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  GenerationVector current = current_of(*snapshot);
  const Digest base = generation_vector_digest(current);
  current.topology = Generation::from_value(9);
  CFN_CHECK(generation_vector_digest(current).value != base.value);
  current = current_of(*snapshot);
  current.resource_count += 1U;
  CFN_CHECK(generation_vector_digest(current).value != base.value);
}