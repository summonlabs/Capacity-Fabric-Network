// Capacity Fabric Network - snapshot evaluation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Evaluation is a pure function of the authoritative inputs plus the clock.
// Order is fixed: validate structure, validate generation binding, derive
// accounting, roll up, analyse fragmentation, verify closure, summarise
// evidence. Any step may reject; a rejected evaluation produces no snapshot.
#ifndef CFN_ENGINE_EVALUATOR_HPP
#define CFN_ENGINE_EVALUATOR_HPP

#include "cfn/core/cancel.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/model/capacity_model.hpp"
#include "cfn/model/degradation.hpp"
#include "cfn/model/demand_shape.hpp"
#include "cfn/model/policy.hpp"
#include "cfn/model/reservation.hpp"
#include "cfn/model/resource.hpp"
#include "cfn/model/topology.hpp"
#include "cfn/engine/snapshot.hpp"

namespace cfn {

/// Authoritative inputs of one evaluation. Every pointer must remain valid for
/// the duration of the call and must not be modified during it.
struct EvaluationInputs {
  const CapacityModel* model = nullptr;
  const CapacityPolicy* policy = nullptr;
  const ResourceCatalog* resources = nullptr;
  const Topology* topology = nullptr;
  const ReservationSnapshot* reservations = nullptr;
  const DegradationSnapshot* degradation = nullptr;
  const FailureDomainCatalog* domains = nullptr;
  /// Optional. When null, fragmentation is not evaluated and the snapshot
  /// reports shape-independent accounting.
  const DemandShape* demand_shape = nullptr;
};

struct EvaluationOptions {
  Limits limits;
  Timestamp now;
  FabricEpoch epoch;
  /// Snapshot identity to record. When null, one is derived deterministically
  /// from the model and the generation vector.
  CapacitySnapshotId snapshot_id;
  Generation snapshot_generation;
  /// Provenance to record on the produced snapshot.
  Provenance provenance;
  /// When true the fragmentation analysis runs. When false the snapshot is
  /// shape-independent.
  bool evaluate_fragmentation = true;
  /// Cooperative cancellation, checked between phases and inside the flow
  /// solver. A cancelled evaluation produces no snapshot.
  CancellationToken cancel;
};

/// Runs one evaluation.
[[nodiscard]] CFN_API Outcome<CapacitySnapshot> evaluate(const EvaluationInputs& inputs,
                                                         const EvaluationOptions& options);

/// Builds the current generation vector from authoritative inputs. Used both
/// to bind a snapshot and, later, to detect that it has gone stale.
[[nodiscard]] CFN_API Outcome<GenerationVector> make_generation_vector(const EvaluationInputs& inputs,
                                                                       Timestamp now, FabricEpoch epoch,
                                                                       const Provenance& provenance);

}  // namespace cfn

#endif  // CFN_ENGINE_EVALUATOR_HPP