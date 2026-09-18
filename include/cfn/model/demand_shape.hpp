// Capacity Fabric Network - demand shape.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A demand shape states what has to be carried between which endpoints, and in
// what granularity. Fragmentation is always evaluated against a shape: the
// same fabric can be perfectly usable for one shape and badly stranded for
// another.
#ifndef CFN_MODEL_DEMAND_SHAPE_HPP
#define CFN_MODEL_DEMAND_SHAPE_HPP

#include <cstdint>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/model/resource.hpp"

namespace cfn {

struct FlowDemand {
  FlowId id;
  ResourceId source;
  ResourceId sink;
  /// Magnitude the shape asks for. Zero means the shape probes how much the
  /// fabric could deliver between the endpoints without asking for anything.
  Capacity magnitude;
  /// Relative order in which competing flows are placed when the shape cannot
  /// be satisfied in full. Lower values are placed first; ties break on the
  /// flow identity so the result is deterministic.
  std::uint32_t priority = 0;

  [[nodiscard]] friend bool operator==(const FlowDemand&, const FlowDemand&) noexcept = default;
};

struct DemandShape {
  DemandShapeId id;
  Generation generation;
  FabricEpoch epoch;
  Provenance provenance;
  std::vector<FlowDemand> flows;
  /// Smallest allocatable slice. Zero means the shape can use any remainder.
  Capacity granularity;

  [[nodiscard]] friend bool operator==(const DemandShape&, const DemandShape&) noexcept = default;
};

[[nodiscard]] CFN_API Outcome<void> validate(const DemandShape& shape, const ResourceCatalog& catalog,
                                             const Limits& limits);

}  // namespace cfn

#endif  // CFN_MODEL_DEMAND_SHAPE_HPP
