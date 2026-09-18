// Capacity Fabric Network - benchmark harness.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The harness measures completed work. Every measured body returns a checksum
// that is accumulated and printed, so a compiler cannot delete the work and a
// reader can confirm the same amount of work was actually performed.
#ifndef CFN_BENCH_HARNESS_HPP
#define CFN_BENCH_HARNESS_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cfn/core/api.hpp"

namespace cfn::bench {

struct Measurement {
  std::string name;
  std::string population;
  std::uint64_t iterations = 0;
  std::uint64_t total_nanos = 0;
  std::uint64_t work_units = 0;
  std::uint64_t checksum = 0;

  [[nodiscard]] double ns_per_iteration() const noexcept;
  [[nodiscard]] double work_units_per_second() const noexcept;
};

class CFN_API Suite {
 public:
  explicit Suite(std::string title, bool json_output = false);

  /// Runs the body the requested number of times and records the completed
  /// wall time together with the accumulated checksum.
  Measurement& measure(std::string name, std::string population, std::uint64_t iterations,
                       const std::function<std::uint64_t(std::uint64_t)>& body);

  [[nodiscard]] const std::vector<Measurement>& measurements() const noexcept { return measurements_; }
  [[nodiscard]] std::string render() const;

 private:
  std::string title_;
  bool json_ = false;
  std::vector<Measurement> measurements_;
};

/// Runs one iteration of a body and returns a checksum.
[[nodiscard]] CFN_API std::uint64_t run_once(const std::function<std::uint64_t()>& body);

}  // namespace cfn::bench

#endif  // CFN_BENCH_HARNESS_HPP
