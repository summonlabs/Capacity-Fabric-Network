// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/engine/snapshot.hpp"

#include <string>

#include "cfn/core/checked.hpp"
#include "cfn/core/hash.hpp"

namespace cfn {

std::string_view to_string(StalenessReason reason) noexcept {
  switch (reason) {
    case StalenessReason::None: return "current";
    case StalenessReason::EpochAdvanced: return "fabric-epoch-advanced";
    case StalenessReason::ModelChanged: return "capacity-model-changed";
    case StalenessReason::PolicyChanged: return "policy-changed";
    case StalenessReason::TopologyChanged: return "topology-changed";
    case StalenessReason::ResourceCatalogChanged: return "resource-catalog-changed";
    case StalenessReason::ResourceSetChanged: return "resource-set-changed";
    case StalenessReason::ReservationChanged: return "reservation-snapshot-changed";
    case StalenessReason::FailureDomainChanged: return "failure-domain-catalog-changed";
    case StalenessReason::DegradationChanged: return "degradation-changed";
    case StalenessReason::DemandShapeChanged: return "demand-shape-changed";
    case StalenessReason::EvidenceExpired: return "evidence-expired";
  }
  return "unknown";
}

Digest generation_vector_digest(const GenerationVector& generations) noexcept {
  std::uint64_t seed = hash::mix(0x2545F4914F6CDD1DULL);
  seed = hash::combine(seed, generations.fabric_epoch.value());
  seed = hash::combine(seed, generations.model.value());
  seed = hash::combine(seed, generations.policy.value());
  seed = hash::combine(seed, generations.topology.value());
  seed = hash::combine(seed, generations.resource_catalog.value());
  seed = hash::combine(seed, generations.reservation_snapshot.value());
  seed = hash::combine(seed, generations.failure_domain_catalog.value());
  seed = hash::combine(seed, generations.degradation.value());
  seed = hash::combine(seed, generations.demand_shape.value());
  seed = hash::combine(seed, generations.resource_set.value);
  seed = hash::combine(seed, generations.resource_count);
  Digest digest;
  digest.value = seed;
  return digest;
}

CapacitySnapshotId make_snapshot_id(const CapacityModelId& model,
                                    const GenerationVector& generations) {
  const Digest digest = generation_vector_digest(generations);
  std::string text("snap-");
  text.append(model.view());
  text.push_back('-');
  static const char* kHex = "0123456789abcdef";
  for (int shift = 60; shift >= 0; shift -= 4) {
    text.push_back(kHex[(digest.value >> static_cast<unsigned>(shift)) & 0x0FULL]);
  }
  if (text.size() > kMaxIdentityBytes) {
    text.resize(kMaxIdentityBytes);
  }
  const auto parsed = CapacitySnapshotId::parse(text);
  if (parsed.has_value()) {
    return *parsed;
  }
  const auto fallback = CapacitySnapshotId::parse("snapshot");
  return fallback.has_value() ? *fallback : CapacitySnapshotId{};
}

const ResourceAccounting* CapacitySnapshot::find(const ResourceId& resource) const noexcept {
  for (const ResourceAccounting& row : per_resource) {
    if (row.resource == resource) {
      return &row;
    }
  }
  return nullptr;
}

Outcome<void> verify_closure(const CapacitySnapshot& snapshot) {
  CFN_RETURN_IF_ERROR(verify_closure(snapshot.rollup));
  if (!snapshot.fragmentation.evaluated) {
    return Outcome<void>();
  }
  const Capacity& usable = snapshot.rollup.usable_total;
  const Capacity& spare = snapshot.rollup.spare_total;
  const Capacity& stranded = snapshot.rollup.stranded_total;
  const Capacity& available = snapshot.rollup.available_total;
  const FragmentationResult& fragmentation = snapshot.fragmentation;

  if (usable.units != fragmentation.satisfied.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: usable capacity is not the placed demand");
  }
  if (spare.units != fragmentation.spare.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: spare capacity is not the deliverable surplus");
  }
  if (stranded.units != fragmentation.stranding.total().units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: stranded capacity is not the sum of its causes");
  }
  checked::Accumulator reachable;
  (void)reachable.add(usable.units);
  (void)reachable.add(spare.units);
  if (!reachable.valid() || reachable.value() != fragmentation.deliverable.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: placed plus spare capacity is not the deliverable capacity");
  }
  checked::Accumulator total;
  (void)total.add(fragmentation.deliverable.units);
  (void)total.add(stranded.units);
  if (!total.valid() || total.value() != available.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: deliverable plus stranded capacity is not available "
                 "capacity");
  }
  if (fragmentation.spare.units < fragmentation.granularity_loss.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: granularity loss exceeds the spare capacity");
  }
  return Outcome<void>();
}

}  // namespace cfn
