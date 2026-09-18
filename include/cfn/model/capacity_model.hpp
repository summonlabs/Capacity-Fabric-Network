// Capacity Fabric Network - owned capacity model definition.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A capacity model is the durable, versioned statement of what to evaluate:
// which policy applies and, optionally, which demand shape the result is
// reported against. Models are owned by this library and persisted by it.
#ifndef CFN_MODEL_CAPACITY_MODEL_HPP
#define CFN_MODEL_CAPACITY_MODEL_HPP

#include <cstdint>
#include <string_view>

#include "cfn/core/bounded.hpp"
#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/core/time.hpp"

namespace cfn {

struct CapacityModel {
  CapacityModelId id;
  Generation generation;
  FabricEpoch epoch;
  PolicyId policy;
  Generation policy_generation;
  /// Optional demand shape binding. A null identity means the model reports
  /// shape-independent accounting and leaves fragmentation unevaluated.
  DemandShapeId demand_shape;
  Generation demand_shape_generation;
  /// Human readable label, bounded.
  BoundedString<96> label;
  Timestamp created_at;
  Provenance provenance;

  [[nodiscard]] bool has_demand_shape() const noexcept { return demand_shape.valid(); }
};

[[nodiscard]] CFN_API Outcome<void> validate(const CapacityModel& model, const Limits& limits);

}  // namespace cfn

#endif  // CFN_MODEL_CAPACITY_MODEL_HPP
