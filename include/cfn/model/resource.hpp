// Capacity Fabric Network - authoritative resource descriptions.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A resource is a capacity-bearing artefact supplied by an authoritative
// source. This library never discovers resources and never decides whether a
// resource is up; it records what was declared or measured, with provenance,
// and it refuses to treat unsupported figures as capacity.
#ifndef CFN_MODEL_RESOURCE_HPP
#define CFN_MODEL_RESOURCE_HPP

#include <compare>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"

namespace cfn {

/// Kind of capacity-bearing artefact. Purely descriptive: the accounting and
/// fragmentation semantics are identical for every kind.
enum class ResourceKind : std::uint8_t {
  Unknown = 0,
  Port = 1,
  Link = 2,
  Conduit = 3,
  Channel = 4,
  Spectrum = 5,
  Queue = 6,
  VirtualCircuit = 7,
  Node = 8,
  Bundle = 9,
};

[[nodiscard]] CFN_API std::string_view to_string(ResourceKind kind) noexcept;
[[nodiscard]] CFN_API bool resource_kind_from_string(std::string_view text, ResourceKind& out) noexcept;

/// A quantity of capacity, in the unit the authoritative source uses
/// (bits per second, slots, lambda equivalents). The unit is a property of the
/// deployment, not of this library.
struct Capacity {
  std::uint64_t units = 0;

  [[nodiscard]] static constexpr Capacity from_units(std::uint64_t units) noexcept { return Capacity{units}; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return units == 0; }
  [[nodiscard]] friend constexpr bool operator==(Capacity, Capacity) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Capacity, Capacity) noexcept = default;
};

struct ResourceRecord {
  ResourceId id;
  Generation generation;
  ResourceKind kind = ResourceKind::Unknown;
  FailureDomainId failure_domain;
  /// Capacity as reported by the source. Only meaningful together with the
  /// authoritative flag below; a non-authoritative figure is reported
  /// capacity, never known capacity.
  Capacity reported_capacity;
  EvidenceClass capacity_evidence = EvidenceClass::Unknown;
  Provenance provenance;
  /// True when the reported capacity may be used as authoritative raw
  /// capacity. False means the magnitude is UNKNOWN and is never spare
  /// capacity.
  bool authoritative = false;
  /// False when the authoritative source has withdrawn the resource.
  bool present = true;
  /// Smallest indivisible allocation unit. Zero means no granularity limit.
  Capacity granularity;

  [[nodiscard]] friend bool operator==(const ResourceRecord&, const ResourceRecord&) noexcept = default;
};

struct ResourceCatalog {
  Generation generation;
  FabricEpoch epoch;
  Provenance provenance;
  std::vector<ResourceRecord> resources;

  [[nodiscard]] const ResourceRecord* find(const ResourceId& id) const noexcept;
  [[nodiscard]] ResourceRecord* find(const ResourceId& id) noexcept;
};

[[nodiscard]] CFN_API Outcome<void> validate(const ResourceRecord& record, const Limits& limits);
[[nodiscard]] CFN_API Outcome<void> validate(const ResourceCatalog& catalog, const Limits& limits);

/// Digest over the sorted (resource id, generation) set. Two catalogs with the
/// same resources at the same generations always produce the same digest, so a
/// snapshot can be bound to the exact resource population it was computed
/// from.
[[nodiscard]] CFN_API Digest resource_set_digest(const ResourceCatalog& catalog);

}  // namespace cfn

#endif  // CFN_MODEL_RESOURCE_HPP
