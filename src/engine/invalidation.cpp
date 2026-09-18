// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/engine/invalidation.hpp"

namespace cfn {

StalenessReason classify_staleness(const CapacitySnapshot& snapshot,
                                   const GenerationVector& current) noexcept {
  const GenerationVector& bound = snapshot.generations;
  if (bound.fabric_epoch != current.fabric_epoch) {
    return StalenessReason::EpochAdvanced;
  }
  if (bound.model != current.model) {
    return StalenessReason::ModelChanged;
  }
  if (bound.policy != current.policy) {
    return StalenessReason::PolicyChanged;
  }
  if (bound.topology != current.topology) {
    return StalenessReason::TopologyChanged;
  }
  if (bound.resource_catalog != current.resource_catalog) {
    return StalenessReason::ResourceCatalogChanged;
  }
  if (bound.resource_set != current.resource_set || bound.resource_count != current.resource_count) {
    return StalenessReason::ResourceSetChanged;
  }
  if (bound.reservation_snapshot != current.reservation_snapshot) {
    return StalenessReason::ReservationChanged;
  }
  if (bound.failure_domain_catalog != current.failure_domain_catalog) {
    return StalenessReason::FailureDomainChanged;
  }
  if (bound.degradation != current.degradation) {
    return StalenessReason::DegradationChanged;
  }
  if (bound.demand_shape != current.demand_shape) {
    return StalenessReason::DemandShapeChanged;
  }
  return StalenessReason::None;
}

Outcome<void> validate_binding(const CapacitySnapshot& snapshot, const GenerationVector& current) {
  const GenerationVector& bound = snapshot.generations;
  if (bound.fabric_epoch != current.fabric_epoch) {
    return Error(ErrorCode::EpochMismatch,
                 "snapshot was computed in a different fabric epoch; it must be recomputed",
                 snapshot.id.view());
  }
  if (bound.model != current.model) {
    return Error(ErrorCode::StaleModel, "capacity model generation has advanced", snapshot.id.view());
  }
  if (bound.policy != current.policy) {
    return Error(ErrorCode::StalePolicy, "policy generation has advanced", snapshot.id.view());
  }
  if (bound.topology != current.topology) {
    return Error(ErrorCode::StaleTopology, "topology generation has advanced", snapshot.id.view());
  }
  if (bound.resource_catalog != current.resource_catalog) {
    return Error(ErrorCode::StaleResource, "resource catalog generation has advanced",
                 snapshot.id.view());
  }
  if (bound.resource_set != current.resource_set || bound.resource_count != current.resource_count) {
    return Error(ErrorCode::StaleResource, "resource population has changed", snapshot.id.view());
  }
  if (bound.reservation_snapshot != current.reservation_snapshot) {
    return Error(ErrorCode::StaleReservation, "reservation snapshot generation has advanced",
                 snapshot.id.view());
  }
  if (bound.failure_domain_catalog != current.failure_domain_catalog) {
    return Error(ErrorCode::StaleFailureDomain, "failure domain catalog generation has advanced",
                 snapshot.id.view());
  }
  if (bound.degradation != current.degradation) {
    return Error(ErrorCode::StaleEvidence, "degradation evidence generation has advanced",
                 snapshot.id.view());
  }
  if (bound.demand_shape != current.demand_shape) {
    return Error(ErrorCode::StaleDemandShape, "demand shape generation has advanced",
                 snapshot.id.view());
  }
  return Outcome<void>();
}

}  // namespace cfn
