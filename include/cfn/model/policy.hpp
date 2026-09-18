// Capacity Fabric Network - capacity policy.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Policy states how much capacity must be protected and how much resilience
// the fabric is required to retain. It never changes observed capacity; it
// only changes what part of the observed capacity may be advertised as usable.
#ifndef CFN_MODEL_POLICY_HPP
#define CFN_MODEL_POLICY_HPP

#include <cstdint>
#include <string_view>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/model/resource.hpp"

namespace cfn {

enum class ResilienceMode : std::uint8_t {
  /// No resilience requirement beyond the recorded deductions.
  None = 0,
  /// The fabric must still carry the shape after the loss of any single
  /// failure domain.
  DomainN1 = 1,
  /// The fabric must still carry the shape after the loss of any two failure
  /// domains, evaluated independently.
  DomainN1N1 = 2,
};

[[nodiscard]] CFN_API std::string_view to_string(ResilienceMode value) noexcept;
[[nodiscard]] CFN_API bool resilience_mode_from_string(std::string_view text, ResilienceMode& out) noexcept;

struct CapacityPolicy {
  PolicyId id;
  Generation generation;
  FabricEpoch epoch;
  /// Protected headroom, as a fraction of authoritative raw capacity in parts
  /// per million.
  std::uint32_t headroom_floor_ppm = 0;
  /// Additional absolute protected headroom, in resource units.
  Capacity headroom_floor_absolute;
  ResilienceMode resilience = ResilienceMode::None;
  /// When true, a resource without authoritative capacity fails the
  /// evaluation instead of being carried as UNKNOWN capacity.
  bool reject_unknown_capacity = false;
  /// When true, a demand that cannot be satisfied in one piece because of the
  /// resource granularity is reported unsatisfied. When false, the largest
  /// admissible slice is reported instead.
  bool require_single_slice = false;
  Provenance provenance;

  [[nodiscard]] friend bool operator==(const CapacityPolicy&, const CapacityPolicy&) noexcept = default;
};

[[nodiscard]] CFN_API Outcome<void> validate(const CapacityPolicy& policy, const Limits& limits);

}  // namespace cfn

#endif  // CFN_MODEL_POLICY_HPP
