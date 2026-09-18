// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Concurrency tests. Threads, cancellation, and shutdown are exercised against
// the real implementation; nothing here is simulated.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "cfn/bench/synthetic.hpp"
#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

struct FabricRig {
  std::unique_ptr<Fabric> fabric;
  text::Scenario scenario;
};

[[nodiscard]] Outcome<FabricRig> make_rig(std::uint32_t resources) {
  bench::SyntheticParams params;
  params.resource_count = resources;
  params.path_width = 4;
  params.seed = 99;
  FabricRig rig;
  rig.scenario = bench::make_synthetic_scenario(params);
  FabricOptions options;
  auto opened = Fabric::open(options);
  if (!opened) {
    return opened.error();
  }
  rig.fabric = std::move(*opened);
  CFN_RETURN_IF_ERROR(rig.fabric->set_resource_catalog(rig.scenario.resources));
  CFN_RETURN_IF_ERROR(rig.fabric->set_topology(rig.scenario.topology));
  CFN_RETURN_IF_ERROR(rig.fabric->set_failure_domains(rig.scenario.domains));
  CFN_RETURN_IF_ERROR(rig.fabric->set_reservations(rig.scenario.reservations));
  CFN_RETURN_IF_ERROR(rig.fabric->set_degradation(rig.scenario.degradation));
  CFN_RETURN_IF_ERROR(rig.fabric->register_policy(rig.scenario.policy));
  CFN_RETURN_IF_ERROR(rig.fabric->register_demand_shape(rig.scenario.demand_shape));
  CFN_RETURN_IF_ERROR(rig.fabric->register_model(rig.scenario.model));
  return rig;
}

}  // namespace

CFN_TEST(concurrency, readers_and_writers_share_the_fabric) {
  auto rig = make_rig(512);
  CFN_REQUIRE_OK(cfn_ctx, rig);

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> computed{0};
  std::atomic<std::uint64_t> rejected{0};
  std::atomic<std::uint64_t> broken{0};

  std::vector<std::thread> readers;
  for (int index = 0; index < 4; ++index) {
    readers.emplace_back([&]() {
      while (!stop.load(std::memory_order_acquire)) {
        const auto snapshot = rig->fabric->compute(rig->scenario.model.id);
        if (!snapshot) {
          rejected.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        if (!verify_closure(*snapshot)) {
          broken.fetch_add(1, std::memory_order_relaxed);
        }
        computed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::thread writer([&]() {
    for (int index = 0; index < 200; ++index) {
      ReservationSnapshot snapshot = rig->scenario.reservations;
      if (!snapshot.reservations.empty()) {
        snapshot.reservations.resize(static_cast<std::size_t>(index % 8));
      }
      (void)rig->fabric->set_reservations(snapshot);
      DegradationSnapshot degradation = rig->scenario.degradation;
      if (!degradation.records.empty()) {
        degradation.records.resize(static_cast<std::size_t>(index % 4));
      }
      (void)rig->fabric->set_degradation(degradation);
    }
  });

  writer.join();
  stop.store(true, std::memory_order_release);
  for (std::thread& reader : readers) {
    reader.join();
  }
  CFN_CHECK(computed.load() > 0);
  CFN_CHECK_EQ(broken.load(), 0ULL);
  CFN_NOTE("computed=" + std::to_string(computed.load()) +
           " rejected=" + std::to_string(rejected.load()));
  CFN_REQUIRE_OK(cfn_ctx, rig->fabric->shutdown());
}

CFN_TEST(concurrency, shutdown_cancels_work_in_flight) {
  auto rig = make_rig(6000);
  CFN_REQUIRE_OK(cfn_ctx, rig);

  std::atomic<bool> started{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> clean{true};

  std::thread worker([&]() {
    while (!rig->fabric->shutting_down()) {
      started.store(true, std::memory_order_release);
      const auto snapshot = rig->fabric->compute(rig->scenario.model.id);
      if (snapshot) {
        if (!verify_closure(*snapshot)) {
          clean.store(false, std::memory_order_relaxed);
        }
      } else if (snapshot.error().code() != ErrorCode::Cancelled &&
                 snapshot.error().code() != ErrorCode::Closed) {
        clean.store(false, std::memory_order_relaxed);
      }
    }
    finished.store(true, std::memory_order_release);
  });

  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  CFN_REQUIRE_OK(cfn_ctx, rig->fabric->shutdown());
  worker.join();
  CFN_CHECK(finished.load());
  CFN_CHECK(clean.load());
  CFN_CHECK_EQ(rig->fabric->active_evaluations(), 0ULL);

  const auto after = rig->fabric->compute(rig->scenario.model.id);
  CFN_REQUIRE_ERROR(cfn_ctx, after, ErrorCode::Closed);
  CFN_REQUIRE_OK(cfn_ctx, rig->fabric->shutdown());
  CFN_CHECK(rig->fabric->shutting_down());
}

CFN_TEST(concurrency, concurrent_evaluations_agree) {
  auto rig = make_rig(256);
  CFN_REQUIRE_OK(cfn_ctx, rig);
  const auto reference = rig->fabric->compute(rig->scenario.model.id);
  CFN_REQUIRE_OK(cfn_ctx, reference);

  std::vector<std::thread> workers;
  std::atomic<std::uint64_t> mismatches{0};
  for (int index = 0; index < 4; ++index) {
    workers.emplace_back([&]() {
      for (int iteration = 0; iteration < 8; ++iteration) {
        const auto snapshot = rig->fabric->compute(rig->scenario.model.id);
        if (!snapshot) {
          mismatches.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        if (snapshot->id.view() != reference->id.view() ||
            snapshot->rollup.usable_total.units != reference->rollup.usable_total.units ||
            snapshot->rollup.stranded_total.units != reference->rollup.stranded_total.units) {
          mismatches.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  CFN_CHECK_EQ(mismatches.load(), 0ULL);
  CFN_REQUIRE_OK(cfn_ctx, rig->fabric->shutdown());
}

CFN_TEST(concurrency, durable_store_accepts_concurrent_writers) {
  cfn::test::TempDirectory directory("cfn-concurrent-store");
  StoreOptions options;
  options.directory = directory.path();
  SystemClock clock;
  RecoveryReport report;
  {
    auto store = CapacityStore::open(options, clock, report);
    CFN_REQUIRE_OK(cfn_ctx, store);
    CapacityPolicy policy;
    policy.id = *PolicyId::parse("concurrent-policy");
    policy.generation = Generation::initial();
    policy.provenance = declared_provenance(cfn::test::test_source(), cfn::test::test_now(), Duration{});
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_policy(policy));

    std::atomic<std::uint64_t> failures{0};
    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
      writers.emplace_back([&, worker]() {
        for (int index = 0; index < 25; ++index) {
          CapacityModel model;
          const std::string name = "model-" + std::to_string(worker) + "-" + std::to_string(index);
          const auto id = CapacityModelId::parse(name);
          if (!id.has_value()) {
            failures.fetch_add(1, std::memory_order_relaxed);
            continue;
          }
          model.id = *id;
          model.generation = Generation::initial();
          model.policy = policy.id;
          model.policy_generation = policy.generation;
          model.provenance = derived_provenance(cfn::test::test_source(), cfn::test::test_now());
          if (!(*store)->put_model(model)) {
            failures.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }
    for (std::thread& writer : writers) {
      writer.join();
    }
    CFN_CHECK_EQ(failures.load(), 0ULL);
    CFN_CHECK_EQ((*store)->models().size(), 100U);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    auto store = CapacityStore::open(options, clock, report);
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_CHECK_EQ((*store)->models().size(), 100U);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
}

CFN_TEST(concurrency, registry_publication_is_atomic) {
  Registry registry(Limits(), FabricEpoch::initial(), cfn::test::test_provenance());
  ResourceCatalog catalog;
  catalog.generation = Generation::initial();
  catalog.provenance = cfn::test::test_provenance();
  for (int index = 0; index < 32; ++index) {
    ResourceRecord record;
    record.id = *ResourceId::parse("r-" + std::to_string(index));
    record.generation = Generation::initial();
    record.reported_capacity = Capacity::from_units(100);
    record.capacity_evidence = EvidenceClass::Measured;
    record.authoritative = true;
    record.provenance = cfn::test::test_provenance();
    catalog.resources.push_back(record);
  }
  CFN_REQUIRE_OK(cfn_ctx, registry.set_resource_catalog(catalog));

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> reads{0};
  std::atomic<std::uint64_t> inconsistent{0};
  std::thread reader([&]() {
    while (!stop.load(std::memory_order_acquire)) {
      const RegistryViewPtr view = registry.view();
      // Every published view is immutable; a reader may see an older or newer
      // population, but never a torn one outside the range the writer produced.
      if (!view->resources.generation.valid() || view->resources.resources.size() < 28U ||
          view->resources.resources.size() > 32U) {
        inconsistent.fetch_add(1, std::memory_order_relaxed);
      }
      reads.fetch_add(1, std::memory_order_relaxed);
    }
  });
  while (reads.load(std::memory_order_relaxed) == 0) {
    std::this_thread::yield();
  }
  for (int index = 0; index < 200; ++index) {
    ResourceCatalog next = catalog;
    next.resources.resize(static_cast<std::size_t>(32 - (index % 4)));
    (void)registry.set_resource_catalog(next);
  }
  stop.store(true, std::memory_order_release);
  reader.join();
  CFN_CHECK_EQ(inconsistent.load(), 0ULL);
  CFN_CHECK(reads.load() > 0);
}