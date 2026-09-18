// Capacity Fabric Network - synthetic population generator.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// SYNTHETIC ONLY. These populations are generated in memory and have no
// relationship to any physical network. Every benchmark and randomized test
// that uses them must label the result as synthetic.
#ifndef CFN_BENCH_SYNTHETIC_HPP
#define CFN_BENCH_SYNTHETIC_HPP

#include <cstdint>
#include <string>

#include "cfn/text/scenario.hpp"

namespace cfn::bench {

struct SyntheticParams {
  std::uint32_t resource_count = 256;
  /// Parallel paths between the two endpoint groups.
  std::uint32_t path_width = 4;
  /// Target fraction of resources carrying a reservation, in parts per million.
  std::uint32_t reservation_density_ppm = 100000;
  std::uint32_t failure_domain_count = 4;
  std::uint32_t flow_count = 2;
  std::uint32_t degraded_count = 0;
  std::uint32_t unhealthy_domain_count = 0;
  std::uint64_t base_capacity = 10000000ULL;
  std::uint32_t headroom_ppm = 50000;
  std::uint64_t seed = 1;
};

/// Builds a synthetic scenario. Deterministic for a given parameter set.
[[nodiscard]] CFN_API text::Scenario make_synthetic_scenario(const SyntheticParams& params);

/// Human readable one line description for benchmark labelling.
[[nodiscard]] CFN_API std::string describe(const SyntheticParams& params);

}  // namespace cfn::bench

#endif  // CFN_BENCH_SYNTHETIC_HPP
