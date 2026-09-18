// Capacity Fabric Network - fragmentation and fit queries.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Fragmentation is capacity that exists in aggregate but cannot serve a demand
// shape. The measurement is exact:
//
//   available_total = satisfied + spare + stranded_total
//
// where "satisfied + spare" is the maximum the shape endpoints could carry
// (deliverable) and "stranded" is everything else. Stranding is attributed to
// segmentation, bottleneck, and failure-domain resilience causes, and the
// three attributions are required to sum back to the exact stranded total.
#ifndef CFN_ENGINE_FRAGMENTATION_HPP
#define CFN_ENGINE_FRAGMENTATION_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "cfn/core/cancel.hpp"
#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/engine/accounting.hpp"
#include "cfn/model/demand_shape.hpp"
#include "cfn/model/policy.hpp"
#include "cfn/model/topology.hpp"

namespace cfn {

/// Outcome of one demand flow inside a shape.
struct FlowFit {
  FlowId id;
  ResourceId source;
  ResourceId sink;
  Capacity requested;
  Capacity admitted;
  Capacity unmet;
  bool satisfied = false;
};

/// Result of evaluating a demand shape against the fabric.
struct FragmentationResult {
  bool evaluated = false;
  /// True when the shape is a single flow, where maximum flow is exact and no
  /// relaxation is involved. False means the reported satisfied figure is the
  /// lower bound of an interval.
  bool exact = false;
  /// Deliverable capacity for the shape endpoints with the resilience
  /// requirement applied. Single-commodity relaxation when the shape has more
  /// than one flow: an upper bound on any feasible allocation.
  Capacity deliverable;
  /// Deliverable capacity before the resilience requirement.
  Capacity deliverable_unresilient;
  /// Placed demand, using a deterministic feasible allocation. Lower bound on
  /// the multi-flow optimum.
  Capacity satisfied;
  /// Trivial upper bound on any allocation: min(total demand, deliverable).
  Capacity satisfied_upper;
  /// deliverable minus satisfied: capacity the shape endpoints could carry but
  /// the demand does not ask for.
  Capacity spare;
  /// Demand lost to allocation granularity.
  Capacity granularity_loss;
  StrandingBreakdown stranding;
  Capacity reserved_stranded;
  std::vector<FlowFit> flows;
  std::vector<ResourceId> bottleneck_resources;
  std::vector<ResourceId> segmentation_resources;
  std::uint32_t resilience_scenarios = 0;
  bool resilience_applied = false;

  [[nodiscard]] friend bool operator==(const FragmentationResult&, const FragmentationResult&) noexcept = default;
};

/// A standalone deterministic fit question.
struct FitQuery {
  ResourceId source;
  ResourceId sink;
  Capacity magnitude;
  Capacity granularity;
  ResilienceMode resilience = ResilienceMode::None;

  [[nodiscard]] friend bool operator==(const FitQuery&, const FitQuery&) noexcept = default;
};

struct FitQueryResult {
  bool satisfiable = false;
  /// Maximum flow between two endpoints is exact: there is no multi-flow
  /// relaxation involved.
  bool exact = true;
  Capacity admitted;
  Capacity deliverable;
  /// Equal to the deliverable capacity by the max-flow min-cut theorem.
  Capacity bottleneck_capacity;
  StrandingBreakdown stranding;
  std::uint32_t resilience_scenarios = 0;
  std::vector<ResourceId> bottleneck_resources;
  std::vector<ResourceId> segmentation_resources;
};

/// Evaluates the whole demand shape. Updates the admissible, stranded, and
/// strand cause fields of the supplied accounting rows.
[[nodiscard]] CFN_API Outcome<FragmentationResult> analyse_fragmentation(
    std::vector<ResourceAccounting>& rows, const Topology& topology, const CapacityPolicy& policy,
    const DemandShape* shape, const Limits& limits, const CancellationToken& cancel = {});

/// Answers a single fit question. Does not evaluate resilience unless the
/// query asks for it.
[[nodiscard]] CFN_API Outcome<FitQueryResult> fit_query(std::vector<ResourceAccounting>& rows,
                                                        const Topology& topology, const FitQuery& query,
                                                        const Limits& limits,
                                                        const CancellationToken& cancel = {});

}  // namespace cfn

#endif  // CFN_ENGINE_FRAGMENTATION_HPP