// Capacity Fabric Network - degradation and failure-domain evidence.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Degraded capacity is capacity that still exists on paper but is not
// deliverable. It is accounted separately from reservations and headroom so
// the deduction chain stays explainable.
#ifndef CFN_MODEL_DEGRADATION_HPP
#define CFN_MODEL_DEGRADATION_HPP

#include <cstdint>
#include <string_view>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/model/resource.hpp"

namespace cfn {

enum class DegradationCause : std::uint8_t {
  Unknown = 0,
  Failure = 1,
  Maintenance = 2,
  Health = 3,
  Thermal = 4,
  ErrorRate = 5,
  Operator = 6,
  Spectrum = 7,
};

[[nodiscard]] CFN_API std::string_view to_string(DegradationCause value) noexcept;
[[nodiscard]] CFN_API bool degradation_cause_from_string(std::string_view text, DegradationCause& out) noexcept;

/// One deduction against one resource. Exactly one of the absolute loss and
/// the fractional loss may be set; supplying both, or neither, is rejected.
struct DegradationRecord {
  ResourceId resource;
  /// Generation of the resource this record applies to.
  Generation resource_generation;
  DegradationCause cause = DegradationCause::Unknown;
  /// Absolute capacity lost, in the same unit as the resource.
  Capacity lost;
  /// Fractional loss of the resource raw capacity, in parts per million.
  std::uint32_t loss_ppm = 0;
  Generation generation;
  Provenance provenance;

  [[nodiscard]] friend bool operator==(const DegradationRecord&, const DegradationRecord&) noexcept = default;
};

struct FailureDomainRecord {
  FailureDomainId id;
  Generation generation;
  /// False when the whole domain is considered lost. A lost domain degrades
  /// every resource inside it to zero deliverable capacity.
  bool healthy = true;
  Provenance provenance;

  [[nodiscard]] friend bool operator==(const FailureDomainRecord&, const FailureDomainRecord&) noexcept = default;
};

struct FailureDomainCatalog {
  Generation generation;
  FabricEpoch epoch;
  Provenance provenance;
  std::vector<FailureDomainRecord> domains;

  [[nodiscard]] const FailureDomainRecord* find(const FailureDomainId& id) const noexcept;
};

struct DegradationSnapshot {
  Generation generation;
  FabricEpoch epoch;
  Provenance provenance;
  std::vector<DegradationRecord> records;
};

[[nodiscard]] CFN_API Outcome<void> validate(const DegradationRecord& record, const Limits& limits);
[[nodiscard]] CFN_API Outcome<void> validate(const FailureDomainCatalog& catalog, const Limits& limits);
[[nodiscard]] CFN_API Outcome<void> validate(const DegradationSnapshot& snapshot, const ResourceCatalog& resources,
                                             const FailureDomainCatalog& domains, const Limits& limits);

}  // namespace cfn

#endif  // CFN_MODEL_DEGRADATION_HPP
