// Capacity Fabric Network - provenance and evidence classification.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Provenance is what separates an observation from a prediction. Nothing in
// this library may promote a prediction, a derived estimate, or a stale record
// into positive authority; the rules are enforced here.
#ifndef CFN_CORE_PROVENANCE_HPP
#define CFN_CORE_PROVENANCE_HPP

#include <compare>
#include <cstdint>
#include <string_view>

#include "cfn/core/identity.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/time.hpp"

namespace cfn {

/// How a capacity figure came to be known. The numerical values are part of
/// the persistence and wire formats.
enum class EvidenceClass : std::uint8_t {
  Unknown = 0,
  /// Supplied by an operator or configuration as a statement of intent.
  Declared = 1,
  /// Obtained by measurement of the running system.
  Measured = 2,
  /// Computed from other evidence inside this library.
  Derived = 3,
  /// Produced by a predictive model. Never authoritative.
  Predicted = 4,
  /// Received from a peer process; trusted only as far as its own class allows.
  Imported = 5,
};

/// Where the evidence originated.
enum class ProvenanceSource : std::uint8_t {
  Unknown = 0,
  Operator = 1,
  Controller = 2,
  TelemetryCollector = 3,
  Computation = 4,
  Persistence = 5,
  PeerNode = 6,
  SyntheticGenerator = 7,
};

[[nodiscard]] CFN_API std::string_view to_string(EvidenceClass value) noexcept;
[[nodiscard]] CFN_API std::string_view to_string(ProvenanceSource value) noexcept;
[[nodiscard]] CFN_API bool evidence_class_from_string(std::string_view text, EvidenceClass& out) noexcept;
[[nodiscard]] CFN_API bool provenance_source_from_string(std::string_view text, ProvenanceSource& out) noexcept;

/// Observation classes describe the world as it is; everything else does not.
[[nodiscard]] constexpr bool is_observation(EvidenceClass value) noexcept {
  return value == EvidenceClass::Declared || value == EvidenceClass::Measured ||
         value == EvidenceClass::Imported;
}
[[nodiscard]] constexpr bool is_prediction(EvidenceClass value) noexcept {
  return value == EvidenceClass::Predicted;
}
/// Only declared and measured evidence may carry positive authority.
[[nodiscard]] constexpr bool may_be_authoritative(EvidenceClass value) noexcept {
  return value == EvidenceClass::Declared || value == EvidenceClass::Measured;
}

/// Full description of a piece of evidence.
struct Provenance {
  EvidenceClass evidence_class = EvidenceClass::Unknown;
  ProvenanceSource source = ProvenanceSource::Unknown;
  EvidenceSourceId source_id{};
  /// Wall-clock time the evidence was produced.
  Timestamp observed_at{};
  /// Freshness window. Zero means no expiry was declared, which still requires
  /// an observation class before the evidence can be used.
  Duration valid_for{};
  /// Per-source monotonic sequence, used to detect replayed or reordered input.
  Sequence sequence{};
  /// Fabric epoch the evidence belongs to.
  FabricEpoch epoch{};
  /// True only when this evidence may drive authoritative decisions.
  bool authoritative = false;

  [[nodiscard]] friend constexpr bool operator==(const Provenance&, const Provenance&) noexcept = default;
};

/// Rejects structurally impossible provenance: unknown source, authoritative
/// predictions, authoritative unknown evidence.
[[nodiscard]] CFN_API Outcome<void> validate(const Provenance& provenance) noexcept;

/// True when the evidence is an authoritative observation, still inside its
/// freshness window at the supplied instant, and carries an established
/// sequence. Missing, stale, derived, and predicted evidence all return false.
[[nodiscard]] CFN_API bool is_fresh(const Provenance& provenance, Timestamp now) noexcept;

/// Age of the evidence at the supplied instant; zero when the instant precedes
/// the observation time.
[[nodiscard]] CFN_API Duration age_of(const Provenance& provenance, Timestamp now) noexcept;

[[nodiscard]] CFN_API Provenance observed_provenance(EvidenceSourceId source_id, ProvenanceSource source,
                                                     Timestamp observed_at, Duration valid_for);
[[nodiscard]] CFN_API Provenance declared_provenance(EvidenceSourceId source_id, Timestamp declared_at,
                                                     Duration valid_for);
[[nodiscard]] CFN_API Provenance predicted_provenance(EvidenceSourceId source_id, Timestamp produced_at,
                                                      FabricEpoch epoch);
[[nodiscard]] CFN_API Provenance derived_provenance(EvidenceSourceId source_id, Timestamp produced_at);

}  // namespace cfn

#endif  // CFN_CORE_PROVENANCE_HPP
