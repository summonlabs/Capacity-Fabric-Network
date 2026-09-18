// Capacity Fabric Network - strict scenario text format.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Line oriented and deliberately unforgiving: an unknown directive, a missing
// required field, an unparsable number, a duplicate identity, or an oversized
// population is a rejection with the line number attached. Nothing is guessed
// and nothing is silently ignored.
#ifndef CFN_TEXT_SCENARIO_HPP
#define CFN_TEXT_SCENARIO_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/time.hpp"
#include "cfn/engine/prediction.hpp"
#include "cfn/model/capacity_model.hpp"
#include "cfn/model/degradation.hpp"
#include "cfn/model/demand_shape.hpp"
#include "cfn/model/policy.hpp"
#include "cfn/model/reservation.hpp"
#include "cfn/model/resource.hpp"
#include "cfn/model/topology.hpp"

namespace cfn::text {

struct Scenario {
  std::string name;
  FabricEpoch epoch;
  ResourceCatalog resources;
  Topology topology;
  FailureDomainCatalog domains;
  ReservationSnapshot reservations;
  DegradationSnapshot degradation;
  CapacityPolicy policy;
  DemandShape demand_shape;
  CapacityModel model;
  std::vector<CapacityObservation> observations;
  bool has_demand_shape = false;
};

/// Parses a scenario. The supplied instant anchors evidence freshness.
[[nodiscard]] CFN_API Outcome<Scenario> parse_scenario(std::string_view input, const Limits& limits,
                                                       Timestamp now);
[[nodiscard]] CFN_API Outcome<Scenario> load_scenario(const std::filesystem::path& path, const Limits& limits,
                                                      Timestamp now);

/// Renders a scenario back into the same format. Round tripping is exact.
[[nodiscard]] CFN_API std::string format_scenario(const Scenario& scenario);

}  // namespace cfn::text

#endif  // CFN_TEXT_SCENARIO_HPP
