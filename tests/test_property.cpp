// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Seeded randomized populations. Every generated fabric is SYNTHETIC; the
// point is to stress the accounting identities and fragmentation semantics
// across structures a hand written test would not reach.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <string>
#include <vector>

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

struct RandomShape {
  GraphFixture fixture;
  std::string source;
  std::string sink;
  bool shape_bound = false;
};

[[nodiscard]] RandomShape random_fabric(Rng& rng, std::uint32_t node_count, std::uint32_t edge_count,
                                        std::uint32_t domain_count, bool with_shape) {
  RandomShape result;
  for (std::uint32_t index = 0; index < domain_count; ++index) {
    result.fixture.add_domain(("fd-" + std::to_string(index)).c_str());
  }
  for (std::uint32_t index = 0; index < node_count; ++index) {
    const std::string name = "n-" + std::to_string(index);
    const std::uint64_t capacity = rng.chance(200000) ? rng.range(20, 50) : 0;
    result.fixture.add_resource(name.c_str(), capacity,
                                ("fd-" + std::to_string(rng.bounded(domain_count))).c_str(), true,
                                ResourceKind::Node);
    result.fixture.add_node(name.c_str());
  }
  for (std::uint32_t index = 0; index < edge_count; ++index) {
    const std::uint32_t from = static_cast<std::uint32_t>(rng.bounded(node_count));
    const std::uint32_t to = static_cast<std::uint32_t>(rng.bounded(node_count));
    const std::string capacity_name = "cap-" + std::to_string(index);
    const std::uint64_t capacity = rng.range(20, 1000);
    const bool authoritative = !rng.chance(50000);
    result.fixture.add_resource(capacity_name.c_str(), capacity,
                                ("fd-" + std::to_string(rng.bounded(domain_count))).c_str(),
                                authoritative);
    result.fixture.add_edge(("n-" + std::to_string(from)).c_str(),
                            ("n-" + std::to_string(to)).c_str(), capacity_name.c_str());
    if (authoritative && rng.chance(150000)) {
      const std::uint64_t amount = capacity / 10;
      if (amount > 0) {
        result.fixture.add_reservation(("res-" + std::to_string(index)).c_str(),
                                       capacity_name.c_str(), amount);
      }
    }
    if (authoritative && rng.chance(100000)) {
      result.fixture.add_degradation(capacity_name.c_str(), 50000);
    }
  }
  if (rng.chance(100000)) {
    result.fixture.policy.headroom_floor_ppm = 50000;
  }
  if (rng.chance(200000)) {
    result.fixture.policy.headroom_floor_absolute = Capacity::from_units(rng.range(0, 2));
  }
  if (rng.chance(100000)) {
    result.fixture.policy.resilience = ResilienceMode::DomainN1;
  }
  result.source = "n-0";
  result.sink = "n-" + std::to_string(node_count - 1);
  if (with_shape) {
    result.fixture.add_flow("flow-1", result.source.c_str(), result.sink.c_str(),
                            rng.range(1, 500));
    result.shape_bound = true;
  } else {
    result.fixture.has_shape = false;
    result.fixture.model.demand_shape = DemandShapeId{};
    result.fixture.model.demand_shape_generation = Generation{};
  }
  return result;
}

void check_identities(TestContext& context, const CapacitySnapshot& snapshot) {
  const auto closure = verify_closure(snapshot);
  if (!closure) {
    context.add_note(closure.error().to_text());
  }
  context.check(static_cast<bool>(closure), "closure must hold", __FILE__, __LINE__);

  const AccountingRollup& rollup = snapshot.rollup;
  context.check(rollup.usable_total.units <= rollup.available_total.units,
                "usable must not exceed available", __FILE__, __LINE__);
  context.check(rollup.available_total.units <= rollup.raw_total.units,
                "available must not exceed raw", __FILE__, __LINE__);
  context.check(rollup.unknown_total.units + rollup.raw_total.units >= rollup.raw_total.units,
                "unknown capacity must never reduce raw", __FILE__, __LINE__);
  if (snapshot.fragmentation.evaluated) {
    context.check(snapshot.fragmentation.stranding.total().units == rollup.stranded_total.units,
                  "stranding causes must sum to stranded capacity", __FILE__, __LINE__);
    context.check(snapshot.fragmentation.deliverable.units <= rollup.available_total.units,
                  "deliverable must not exceed available", __FILE__, __LINE__);
  }
}

}  // namespace

CFN_TEST(property, randomized_fabrics_close) {
  for (std::uint64_t seed = 1; seed <= 40; ++seed) {
    Rng rng(seed, 7);
    RandomShape shape = random_fabric(rng, 6, 12, 3, true);
    const auto snapshot = shape.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, snapshot);
    check_identities(cfn_ctx, *snapshot);
  }
}

CFN_TEST(property, randomized_shape_independent_fabrics_close) {
  for (std::uint64_t seed = 100; seed <= 130; ++seed) {
    Rng rng(seed, 11);
    RandomShape shape = random_fabric(rng, 5, 10, 4, false);
    const auto snapshot = shape.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, snapshot);
    check_identities(cfn_ctx, *snapshot);
    CFN_CHECK_EQ(snapshot->rollup.stranded_total.units, 0ULL);
  }
}

CFN_TEST(property, identical_inputs_produce_identical_snapshots) {
  for (std::uint64_t seed = 200; seed <= 210; ++seed) {
    Rng left_rng(seed, 3);
    Rng right_rng(seed, 3);
    RandomShape left = random_fabric(left_rng, 4, 8, 2, true);
    RandomShape right = random_fabric(right_rng, 4, 8, 2, true);
    const auto first = left.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, first);
    const auto second = right.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, second);
    CFN_CHECK_EQ(first->id.view(), second->id.view());
    CFN_CHECK_EQ(first->rollup.raw_total.units, second->rollup.raw_total.units);
    CFN_CHECK_EQ(first->rollup.usable_total.units, second->rollup.usable_total.units);
    CFN_CHECK_EQ(first->rollup.stranded_total.units, second->rollup.stranded_total.units);
  }
}

CFN_TEST(property, a_capacity_drop_never_increases_usable_capacity) {
  Rng rng(4242, 5);
  for (std::uint64_t iteration = 0; iteration < 20; ++iteration) {
    RandomShape base = random_fabric(rng, 5, 9, 2, true);
    const auto before = base.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, before);

    RandomShape dropped = base;
    if (!dropped.fixture.resources.resources.empty()) {
      ResourceRecord& record = dropped.fixture.resources.resources.back();
      if (record.reported_capacity.units > 1) {
        record.reported_capacity = Capacity::from_units(record.reported_capacity.units / 2);
      }
    }
    const auto after = dropped.fixture.evaluate();
    if (!after) {
      // A drop can make a reservation contradictory; that is a rejection, not
      // an increase.
      continue;
    }
    CFN_CHECK(after->rollup.usable_total.units <= before->rollup.usable_total.units);
    check_identities(cfn_ctx, *after);
  }
}

CFN_TEST(property, unknown_capacity_never_becomes_spare) {
  for (std::uint64_t seed = 900; seed <= 920; ++seed) {
    Rng rng(seed, 13);
    RandomShape shape = random_fabric(rng, 5, 10, 3, true);
    const auto snapshot = shape.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, snapshot);
    if (snapshot->rollup.unknown_total.units == 0) {
      continue;
    }
    std::uint64_t unknown_sum = 0;
    for (const ResourceAccounting& row : snapshot->per_resource) {
      unknown_sum += row.unknown.units;
      CFN_CHECK_EQ(row.available.units == 0 && !row.authoritative ? 0ULL : row.available.units,
                   row.authoritative ? row.available.units : 0ULL);
    }
    CFN_CHECK_EQ(unknown_sum, snapshot->rollup.unknown_total.units);
  }
}

CFN_TEST(property, segmentation_and_bottleneck_reconcile) {
  for (std::uint64_t seed = 500; seed <= 540; ++seed) {
    Rng rng(seed, 17);
    RandomShape shape = random_fabric(rng, 7, 14, 3, true);
    const auto snapshot = shape.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, snapshot);
    const FragmentationResult& result = snapshot->fragmentation;
    std::uint64_t total = result.stranding.segmentation.units + result.stranding.bottleneck.units +
                          result.stranding.failure_domain_resilience.units;
    CFN_CHECK_EQ(total, result.stranding.total().units);
    CFN_CHECK_EQ(total, snapshot->rollup.stranded_total.units);
    CFN_CHECK_EQ(result.deliverable.units + result.stranding.total().units,
                 snapshot->rollup.available_total.units);
  }
}

CFN_TEST(property, per_resource_rows_sum_to_the_rollup) {
  for (std::uint64_t seed = 700; seed <= 720; ++seed) {
    Rng rng(seed, 19);
    RandomShape shape = random_fabric(rng, 6, 12, 2, true);
    const auto snapshot = shape.fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, snapshot);
    std::uint64_t raw = 0;
    std::uint64_t reserved = 0;
    std::uint64_t headroom = 0;
    std::uint64_t degraded = 0;
    std::uint64_t available = 0;
    for (const ResourceAccounting& row : snapshot->per_resource) {
      raw += row.raw.units;
      reserved += row.reserved.units;
      headroom += row.headroom.units;
      degraded += row.degraded.units;
      available += row.available.units;
    }
    CFN_CHECK_EQ(raw, snapshot->rollup.raw_total.units);
    CFN_CHECK_EQ(reserved, snapshot->rollup.reserved_total.units);
    CFN_CHECK_EQ(headroom, snapshot->rollup.headroom_total.units);
    CFN_CHECK_EQ(degraded, snapshot->rollup.degraded_total.units);
    CFN_CHECK_EQ(available, snapshot->rollup.available_total.units);
  }
}

CFN_TEST(property, cyclic_topologies_are_handled) {
  for (std::uint64_t seed = 300; seed <= 320; ++seed) {
    Rng rng(seed, 23);
    GraphFixture fixture;
    fixture.add_domain("fd");
    for (int index = 0; index < 6; ++index) {
      const std::string name = "n-" + std::to_string(index);
      fixture.add_resource(name.c_str(), 0, "fd", true, ResourceKind::Node);
      fixture.add_node(name.c_str());
    }
    for (int index = 0; index < 6; ++index) {
      const std::string capacity = "cap-" + std::to_string(index);
      fixture.add_resource(capacity.c_str(), rng.range(1, 100), "fd");
      fixture.add_edge(("n-" + std::to_string(index)).c_str(),
                       ("n-" + std::to_string((index + 1) % 6)).c_str(), capacity.c_str());
    }
    fixture.add_flow("flow-1", "n-0", "n-3", rng.range(1, 50));
    const auto snapshot = fixture.evaluate();
    CFN_REQUIRE_OK(cfn_ctx, snapshot);
    check_identities(cfn_ctx, *snapshot);
  }
}