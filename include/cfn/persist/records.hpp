// Capacity Fabric Network - durable record shapes.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_PERSIST_RECORDS_HPP
#define CFN_PERSIST_RECORDS_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cfn/core/bounded.hpp"
#include "cfn/core/identity.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/core/time.hpp"
#include "cfn/engine/snapshot.hpp"
#include "cfn/model/capacity_model.hpp"
#include "cfn/model/demand_shape.hpp"
#include "cfn/model/policy.hpp"

namespace cfn {

/// Compact history entry for a completed evaluation. The full snapshot is not
/// durable: only the identity, the binding, and the totals are kept.
struct SnapshotRecord {
  CapacitySnapshotId id;
  Generation generation;
  CapacityModelId model;
  GenerationVector generations;
  AccountingRollup rollup;
  Timestamp recorded_at;
};

/// Reference to external evidence. The magnitude itself is never restored from
/// disk: after a restart the reference is a marker that the evidence must be
/// re-observed before it can be used again.
struct EvidenceReference {
  EvidenceSourceId source;
  Generation generation;
  Timestamp last_observed_at;
  Duration valid_for;
  BoundedString<160> locator;
  /// Always true after recovery. Set false only by a live observation in the
  /// current boot incarnation.
  bool requires_revalidation = true;
};

/// Categories of live authority. None of these survive a restart.
enum class LiveAuthorityKind : std::uint8_t {
  Publisher = 0,
  Worker = 1,
  Lease = 2,
  TelemetryFreshness = 3,
};

[[nodiscard]] CFN_API std::string_view to_string(LiveAuthorityKind kind) noexcept;
[[nodiscard]] CFN_API bool live_authority_kind_from_string(std::string_view text, LiveAuthorityKind& out) noexcept;

/// A claim that is only valid while the granting process is alive. Persisting
/// it is required for audit, and restoring it is forbidden.
struct LiveAuthorityRecord {
  LiveAuthorityKind kind = LiveAuthorityKind::Publisher;
  GenericId holder;
  FabricEpoch epoch;
  BootIncarnation boot;
  Timestamp granted_at;
  Duration ttl;
  Sequence sequence;
};

/// The compacted durable state. Records applied after the checkpoint are
/// replayed from the journal on top of this.
struct StoreState {
  /// Highest journal sequence already folded into this checkpoint.
  std::uint64_t last_sequence = 0;
  FabricEpoch epoch;
  BootIncarnation boot;
  /// False when the previous incarnation did not close the store cleanly. An
  /// unclean shutdown advances the fabric epoch on the next open.
  bool clean_shutdown = false;
  std::map<PolicyId, CapacityPolicy> policies;
  std::map<DemandShapeId, DemandShape> demand_shapes;
  std::map<CapacityModelId, CapacityModel> models;
  std::vector<SnapshotRecord> history;
  std::map<EvidenceSourceId, EvidenceReference> evidence_references;
  std::map<GenericId, LiveAuthorityRecord> live_authority;
};

}  // namespace cfn

#endif  // CFN_PERSIST_RECORDS_HPP