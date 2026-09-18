// Capacity Fabric Network - authoritative reservation snapshots.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The library consumes reservations; it never creates, releases, or arbitrates
// them. A reservation is only accepted when it binds to the exact resource
// generation it was made against, so a stale reservation can never be counted
// against a resource that has since changed.
#ifndef CFN_MODEL_RESERVATION_HPP
#define CFN_MODEL_RESERVATION_HPP

#include <cstdint>
#include <string_view>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/model/resource.hpp"

namespace cfn {

enum class ReservationClass : std::uint8_t {
  Unknown = 0,
  /// Capacity committed to a service that is expected to use it.
  Committed = 1,
  /// Capacity held back by an external authority.
  Protected = 2,
  /// Capacity that may not be reclaimed while the reservation lives.
  Pinned = 3,
  /// Capacity that may be reclaimed when demand requires it.
  BestEffort = 4,
};

[[nodiscard]] CFN_API std::string_view to_string(ReservationClass value) noexcept;
[[nodiscard]] CFN_API bool reservation_class_from_string(std::string_view text, ReservationClass& out) noexcept;
/// Best-effort reservations are the only class that a fragmentation fit
/// decision may reclaim; every other class is a hard deduction.
[[nodiscard]] constexpr bool is_reclaimable(ReservationClass value) noexcept {
  return value == ReservationClass::BestEffort;
}

struct Reservation {
  ReservationId id;
  ResourceId resource;
  /// Generation of the resource this reservation was created against.
  Generation resource_generation;
  ReservationClass reservation_class = ReservationClass::Unknown;
  Capacity amount;
  std::uint32_t priority = 0;
  Generation generation;
  Provenance provenance;

  [[nodiscard]] friend bool operator==(const Reservation&, const Reservation&) noexcept = default;
};

struct ReservationSnapshot {
  Generation generation;
  FabricEpoch epoch;
  Provenance provenance;
  std::vector<Reservation> reservations;

  [[nodiscard]] const Reservation* find(const ReservationId& id) const noexcept;
};

/// Structural validation plus consistency with the resource catalog: every
/// reservation must name a present resource at its current generation, and the
/// per-resource total must not exceed the authoritative raw capacity.
[[nodiscard]] CFN_API Outcome<void> validate(const ReservationSnapshot& snapshot, const ResourceCatalog& catalog,
                                             const Limits& limits);

}  // namespace cfn

#endif  // CFN_MODEL_RESERVATION_HPP
