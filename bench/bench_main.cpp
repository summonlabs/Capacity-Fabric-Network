// Capacity Fabric Network - synthetic benchmarks.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// EVERY population in this program is SYNTHETIC. The numbers describe completed
// capacity computation over generated graphs; they say nothing about any
// physical network, and no such claim is made anywhere in this project.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cfn/bench/harness.hpp"
#include "cfn/bench/synthetic.hpp"
#include "cfn/cfn.hpp"

namespace {

[[nodiscard]] bool load_fabric(const cfn::text::Scenario& scenario, const cfn::Limits& limits,
                               std::unique_ptr<cfn::Fabric>& fabric) {
  cfn::FabricOptions options;
  options.limits = limits;
  auto opened = cfn::Fabric::open(options);
  if (!opened) {
    std::fprintf(stderr, "fabric open failed: %s\n", opened.error().to_text().c_str());
    return false;
  }
  fabric = std::move(*opened);
  if (!fabric->set_resource_catalog(scenario.resources)) return false;
  if (!fabric->set_topology(scenario.topology)) return false;
  if (!fabric->set_failure_domains(scenario.domains)) return false;
  if (!fabric->set_reservations(scenario.reservations)) return false;
  if (!fabric->set_degradation(scenario.degradation)) return false;
  if (!fabric->register_policy(scenario.policy)) return false;
  if (scenario.has_demand_shape && !fabric->register_demand_shape(scenario.demand_shape)) return false;
  if (!fabric->register_model(scenario.model)) return false;
  return true;
}

/// A structure-dependent reduction of the completed answer. It mixes the
/// snapshot identity and every per-resource result, so it varies with the
/// population instead of collapsing to an algebraic constant, and a reader can
/// confirm that the same amount of work produced the same answer.
[[nodiscard]] std::uint64_t checksum_of(const cfn::CapacitySnapshot& snapshot) {
  std::uint64_t value = cfn::hash::combine(0x9E3779B97F4A7C15ULL, snapshot.id.hash());
  value = cfn::hash::combine(value, snapshot.rollup.raw_total.units);
  value = cfn::hash::combine(value, snapshot.rollup.usable_total.units);
  value = cfn::hash::combine(value, snapshot.rollup.stranded_total.units);
  value = cfn::hash::combine(value, snapshot.rollup.unknown_total.units);
  value = cfn::hash::combine(value, snapshot.fragmentation.stranding.segmentation.units);
  value = cfn::hash::combine(value, snapshot.fragmentation.stranding.bottleneck.units);
  for (const cfn::ResourceAccounting& row : snapshot.per_resource) {
    value = cfn::hash::combine_id(value, row.resource);
    value = cfn::hash::combine(value, row.available.units);
    value = cfn::hash::combine(value, row.stranded.units);
    value = cfn::hash::combine(value, static_cast<std::uint64_t>(row.strand_cause));
  }
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  bool json = false;
  std::uint64_t iterations = 0;
  std::uint64_t seed = 20260101ULL;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--json") {
      json = true;
    } else if (argument.rfind("--iterations=", 0) == 0) {
      iterations = std::strtoull(std::string(argument.substr(13)).c_str(), nullptr, 10);
    } else if (argument.rfind("--seed=", 0) == 0) {
      seed = std::strtoull(std::string(argument.substr(7)).c_str(), nullptr, 10);
    }
  }

  const cfn::Limits limits;
  cfn::bench::Suite suite("Capacity Fabric Network synthetic benchmarks", json);
  // A measurement whose work did not complete is not a measurement. Any failed
  // iteration is counted, reported, and reflected in the exit status.
  std::uint64_t failed_iterations = 0;
  std::string first_failure;

  struct Case {
    std::string name;
    cfn::bench::SyntheticParams params;
    std::uint64_t rounds;
    bool fit;
  };

  std::vector<Case> cases;
  auto add = [&](const char* name, std::uint32_t resources, std::uint32_t width,
                 std::uint32_t reservation_ppm, std::uint32_t domains, std::uint32_t degraded,
                 std::uint32_t unhealthy, std::uint64_t seed_offset, std::uint64_t default_rounds,
                 bool fit) {
    Case entry;
    entry.name = name;
    entry.params.resource_count = resources;
    entry.params.path_width = width;
    entry.params.reservation_density_ppm = reservation_ppm;
    entry.params.failure_domain_count = domains;
    entry.params.degraded_count = degraded;
    entry.params.unhealthy_domain_count = unhealthy;
    entry.params.seed = seed + seed_offset;
    entry.rounds = iterations == 0 ? default_rounds : iterations;
    entry.fit = fit;
    cases.push_back(entry);
  };

  add("resources-256", 256, 4, 100000, 4, 0, 0, 0, 16, false);
  add("resources-4096", 4096, 8, 100000, 8, 0, 0, 1, 8, false);
  add("resources-32768", 32768, 16, 100000, 16, 0, 0, 2, 4, false);
  add("path-width-1", 2048, 1, 100000, 8, 0, 0, 3, 8, false);
  add("path-width-32", 2048, 32, 100000, 8, 0, 0, 4, 8, false);
  add("reservations-0", 2048, 8, 0, 8, 0, 0, 5, 8, false);
  add("reservations-900000", 2048, 8, 900000, 8, 0, 0, 6, 8, false);
  add("failure-domains-64", 2048, 8, 100000, 64, 0, 0, 7, 8, false);
  add("degraded-and-unhealthy", 2048, 8, 100000, 8, 64, 1, 8, 8, false);
  add("fit-queries", 1024, 8, 100000, 8, 0, 0, 9, 64, true);

  for (const Case& entry : cases) {
    const cfn::text::Scenario scenario = cfn::bench::make_synthetic_scenario(entry.params);
    const std::string population = cfn::bench::describe(entry.params);
    std::unique_ptr<cfn::Fabric> fabric;
    if (!load_fabric(scenario, limits, fabric)) {
      return 1;
    }
    const std::size_t resource_count = scenario.resources.resources.size();

    if (entry.fit) {
      cfn::FitQuery query;
      const auto source = cfn::ResourceId::parse("src");
      const auto sink = cfn::ResourceId::parse("dst");
      if (!source.has_value() || !sink.has_value()) {
        return 1;
      }
      query.source = *source;
      query.sink = *sink;
      query.magnitude = cfn::Capacity::from_units(1000000);
      cfn::bench::Measurement& measurement = suite.measure(
          "fit-query", population, entry.rounds, [&](std::uint64_t) -> std::uint64_t {
            const auto answer = fabric->fit(scenario.policy.id, query);
            if (!answer) {
              failed_iterations += 1;
              if (first_failure.empty()) {
                first_failure = answer.error().to_text();
              }
              return 0;
            }
            std::uint64_t value = cfn::hash::combine(0x2545F4914F6CDD1DULL, answer->admitted.units);
            value = cfn::hash::combine(value, answer->deliverable.units);
            value = cfn::hash::combine(value, answer->stranding.bottleneck.units);
            value = cfn::hash::combine(value, answer->stranding.segmentation.units);
            value = cfn::hash::combine(value, answer->stranding.failure_domain_resilience.units);
            for (const cfn::ResourceId& id : answer->bottleneck_resources) {
              value = cfn::hash::combine_id(value, id);
            }
            return value;
          });
      measurement.work_units = entry.rounds * resource_count;
      continue;
    }

    cfn::bench::Measurement& measurement = suite.measure(
        entry.name, population, entry.rounds, [&](std::uint64_t) -> std::uint64_t {
          const auto snapshot = fabric->compute(scenario.model.id);
          if (!snapshot) {
            failed_iterations += 1;
            if (first_failure.empty()) {
              first_failure = snapshot.error().to_text();
            }
            return 0;
          }
          return checksum_of(*snapshot);
        });
    measurement.work_units = entry.rounds * resource_count;
  }

  std::printf("%s\n", suite.render().c_str());
  if (failed_iterations != 0) {
    std::fprintf(stderr,
                 "benchmark did not complete: %llu iteration(s) failed; first failure: %s\n",
                 static_cast<unsigned long long>(failed_iterations), first_failure.c_str());
    return 1;
  }
  return 0;
}