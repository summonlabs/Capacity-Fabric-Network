// Capacity Fabric Network - capacity accounting.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The accounting identities enforced here are the core of the library:
//
//   raw_total = reserved_total + headroom_total + degraded_total + available_total
//   available_total = usable_total + spare_total + stranded_total
//
// Every term is an exact integer sum over the resource population. A term that
// does not close is a rejected evaluation, never a rounded result.
#ifndef CFN_ENGINE_ACCOUNTING_HPP
#define CFN_ENGINE_ACCOUNTING_HPP

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/model/degradation.hpp"
#include "cfn/model/policy.hpp"
#include "cfn/model/reservation.hpp"
#include "cfn/model/resource.hpp"
#include "cfn/model/topology.hpp"

namespace cfn {

/// Why a unit of capacity cannot serve the bound demand shape.
enum class StrandCause : std::uint8_t {
  None = 0,
  /// The capacity lies on no admissible path between the shape endpoints.
  Segmentation = 1,
  /// The capacity is reachable but separated from the sink by a saturated
  /// minimum cut.
  Bottleneck = 2,
  /// The capacity is only reachable if a failure domain is allowed to fail.
  FailureDomainResilience = 3,
};

inline constexpr std::size_t kStrandCauseCount = 4;

[[nodiscard]] CFN_API std::string_view to_string(StrandCause cause) noexcept;

struct StrandingBreakdown {
  Capacity segmentation;
  Capacity bottleneck;
  Capacity failure_domain_resilience;

  [[nodiscard]] Capacity total() const noexcept;
  [[nodiscard]] Capacity get(StrandCause cause) const noexcept;
};

/// Per-resource accounting. Every field is derived from the authoritative
/// inputs recorded in the snapshot generation vector.
struct ResourceAccounting {
  ResourceId resource;
  Generation generation;
  FailureDomainId failure_domain;
  ResourceKind kind = ResourceKind::Unknown;
  EvidenceClass evidence_class = EvidenceClass::Unknown;
  bool authoritative = false;
  bool present = true;
  bool domain_healthy = true;
  /// Capacity exactly as supplied by the source.
  Capacity reported;
  /// Authoritative raw capacity. Zero when the resource is not authoritative.
  Capacity raw;
  /// Reported magnitude that lacks authority. Never counted as usable.
  Capacity unknown;
  Capacity reserved;
  Capacity headroom;
  Capacity degraded;
  /// raw minus reserved minus headroom minus degraded. Never negative; a
  /// negative value is a rejected contradictory snapshot.
  Capacity available;
  /// True when the resource lies on at least one admissible path for the bound
  /// shape. Always true when no shape is bound.
  bool admissible = true;
  /// Remaining capacity after the fit solve. Only meaningful when a shape was
  /// evaluated.
  Capacity stranded;
  StrandCause strand_cause = StrandCause::None;

  [[nodiscard]] friend bool operator==(const ResourceAccounting&, const ResourceAccounting&) noexcept = default;
};

struct AccountingRollup {
  Capacity raw_total;
  Capacity unknown_total;
  Capacity reserved_total;
  Capacity headroom_total;
  Capacity degraded_total;
  Capacity available_total;
  Capacity usable_total;
  Capacity spare_total;
  Capacity stranded_total;
  StrandingBreakdown stranding;
  /// Reserved capacity that sits on resources which cannot serve the bound
  /// shape. Informational: reserved capacity is already deducted from
  /// available capacity, so this is never added to the identity.
  Capacity reserved_stranded;
  bool fragmentation_evaluated = false;
  std::uint32_t resource_count = 0;
  std::uint32_t present_resource_count = 0;
  std::uint32_t authoritative_resource_count = 0;
  std::uint32_t unknown_resource_count = 0;

  [[nodiscard]] friend bool operator==(const AccountingRollup&, const AccountingRollup&) noexcept = default;
};

/// Derives per-resource accounting from authoritative inputs. Rejects
/// contradictory inputs (reservation, headroom and degradation together
/// exceeding raw capacity; degradation records that do not match the resource
/// generation; records naming resources that are absent from the catalog).
[[nodiscard]] CFN_API Outcome<std::vector<ResourceAccounting>> derive_accounting(
    const ResourceCatalog& resources, const Topology& topology, const ReservationSnapshot& reservations,
    const DegradationSnapshot& degradation, const FailureDomainCatalog& domains, const CapacityPolicy& policy,
    const Limits& limits);

/// Sums per-resource accounting into a rollup. Fails on overflow rather than
/// wrapping.
[[nodiscard]] CFN_API Outcome<AccountingRollup> roll_up(const std::vector<ResourceAccounting>& rows);

/// Verifies the accounting identities against the recorded totals. Returns a
/// Contradictory error naming the identity that failed.
[[nodiscard]] CFN_API Outcome<void> verify_closure(const AccountingRollup& rollup);

}  // namespace cfn

#endif  // CFN_ENGINE_ACCOUNTING_HPP