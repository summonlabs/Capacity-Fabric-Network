// Capacity Fabric Network - generation-bound capacity snapshots.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A snapshot is only meaningful together with the exact generations it was
// computed from. Every authoritative input carries a generation; the snapshot
// records all of them, plus a digest of the resource population, so a later
// reader can prove whether the answer still applies.
#ifndef CFN_ENGINE_SNAPSHOT_HPP
#define CFN_ENGINE_SNAPSHOT_HPP

#include <cstdint>
#include <string_view>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/core/time.hpp"
#include "cfn/engine/accounting.hpp"
#include "cfn/engine/fragmentation.hpp"

namespace cfn {

/// The complete generation binding of a snapshot.
struct GenerationVector {
  FabricEpoch fabric_epoch;
  Generation model;
  Generation policy;
  Generation topology;
  Generation resource_catalog;
  Generation reservation_snapshot;
  Generation failure_domain_catalog;
  Generation degradation;
  Generation demand_shape;
  /// Digest over the sorted resource population used for the evaluation.
  Digest resource_set;
  std::uint32_t resource_count = 0;
  Timestamp computed_at;
  Instant computed_monotonic;
  Provenance provenance;

  [[nodiscard]] friend bool operator==(const GenerationVector&, const GenerationVector&) noexcept = default;
};

[[nodiscard]] CFN_API Digest generation_vector_digest(const GenerationVector& generations) noexcept;

/// Deterministic snapshot identity derived from the model and the generation
/// vector: identical inputs always produce an identical identity.
[[nodiscard]] CFN_API CapacitySnapshotId make_snapshot_id(const CapacityModelId& model,
                                                          const GenerationVector& generations);

/// Why a stored snapshot no longer applies.
enum class StalenessReason : std::uint8_t {
  None = 0,
  EpochAdvanced = 1,
  ModelChanged = 2,
  PolicyChanged = 3,
  TopologyChanged = 4,
  ResourceCatalogChanged = 5,
  ResourceSetChanged = 6,
  ReservationChanged = 7,
  FailureDomainChanged = 8,
  DegradationChanged = 9,
  DemandShapeChanged = 10,
  EvidenceExpired = 11,
};

[[nodiscard]] CFN_API std::string_view to_string(StalenessReason reason) noexcept;

/// How much of the answer rests on authoritative, fresh evidence.
struct ConfidenceSummary {
  std::uint32_t present_resource_count = 0;
  std::uint32_t authoritative_resource_count = 0;
  std::uint32_t unknown_resource_count = 0;
  std::uint32_t unhealthy_domain_count = 0;
  bool all_present_resources_authoritative = false;
  bool exact_fit = false;
  /// Deterministic evidence-completeness figure in parts per million. This is
  /// not a probability and not a prediction confidence.
  std::uint32_t evidence_completeness_ppm = 0;
};

struct CapacitySnapshot {
  CapacitySnapshotId id;
  Generation generation;
  CapacityModelId model;
  GenerationVector generations;
  AccountingRollup rollup;
  FragmentationResult fragmentation;
  std::vector<ResourceAccounting> per_resource;
  ConfidenceSummary confidence;
  Provenance provenance;
  std::uint32_t closure_checks = 0;
  bool closure_verified = false;

  [[nodiscard]] const ResourceAccounting* find(const ResourceId& id) const noexcept;
};

/// Re-derives and verifies every accounting identity recorded in the snapshot.
[[nodiscard]] CFN_API Outcome<void> verify_closure(const CapacitySnapshot& snapshot);

}  // namespace cfn

#endif  // CFN_ENGINE_SNAPSHOT_HPP
