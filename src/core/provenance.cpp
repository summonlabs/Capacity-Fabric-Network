// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/core/provenance.hpp"

#include <string>

namespace cfn {

std::string_view to_string(EvidenceClass value) noexcept {
  switch (value) {
    case EvidenceClass::Unknown: return "unknown";
    case EvidenceClass::Declared: return "declared";
    case EvidenceClass::Measured: return "measured";
    case EvidenceClass::Derived: return "derived";
    case EvidenceClass::Predicted: return "predicted";
    case EvidenceClass::Imported: return "imported";
  }
  return "unknown";
}

std::string_view to_string(ProvenanceSource value) noexcept {
  switch (value) {
    case ProvenanceSource::Unknown: return "unknown";
    case ProvenanceSource::Operator: return "operator";
    case ProvenanceSource::Controller: return "controller";
    case ProvenanceSource::TelemetryCollector: return "telemetry-collector";
    case ProvenanceSource::Computation: return "computation";
    case ProvenanceSource::Persistence: return "persistence";
    case ProvenanceSource::PeerNode: return "peer-node";
    case ProvenanceSource::SyntheticGenerator: return "synthetic-generator";
  }
  return "unknown";
}

bool evidence_class_from_string(std::string_view text, EvidenceClass& out) noexcept {
  if (text == "unknown") { out = EvidenceClass::Unknown; return true; }
  if (text == "declared") { out = EvidenceClass::Declared; return true; }
  if (text == "measured") { out = EvidenceClass::Measured; return true; }
  if (text == "derived") { out = EvidenceClass::Derived; return true; }
  if (text == "predicted") { out = EvidenceClass::Predicted; return true; }
  if (text == "imported") { out = EvidenceClass::Imported; return true; }
  return false;
}

bool provenance_source_from_string(std::string_view text, ProvenanceSource& out) noexcept {
  if (text == "unknown") { out = ProvenanceSource::Unknown; return true; }
  if (text == "operator") { out = ProvenanceSource::Operator; return true; }
  if (text == "controller") { out = ProvenanceSource::Controller; return true; }
  if (text == "telemetry-collector") { out = ProvenanceSource::TelemetryCollector; return true; }
  if (text == "computation") { out = ProvenanceSource::Computation; return true; }
  if (text == "persistence") { out = ProvenanceSource::Persistence; return true; }
  if (text == "peer-node") { out = ProvenanceSource::PeerNode; return true; }
  if (text == "synthetic-generator") { out = ProvenanceSource::SyntheticGenerator; return true; }
  return false;
}

Outcome<void> validate(const Provenance& provenance) noexcept {
  if (provenance.source == ProvenanceSource::Unknown) {
    return Error(ErrorCode::NotAuthoritative, "provenance has no source");
  }
  if (provenance.authoritative && provenance.evidence_class == EvidenceClass::Unknown) {
    return Error(ErrorCode::NotAuthoritative,
                 "unknown evidence class cannot be authoritative");
  }
  if (provenance.authoritative && !may_be_authoritative(provenance.evidence_class)) {
    return Error(ErrorCode::NotAuthoritative,
                 "only declared and measured evidence may be authoritative",
                 to_string(provenance.evidence_class));
  }
  return Outcome<void>();
}

bool is_fresh(const Provenance& provenance, Timestamp now) noexcept {
  if (!provenance.authoritative) {
    return false;
  }
  if (!may_be_authoritative(provenance.evidence_class)) {
    return false;
  }
  if (provenance.valid_for.is_zero()) {
    return true;
  }
  if (!provenance.valid_for.is_positive()) {
    return false;
  }
  Duration age{};
  if (!elapsed(now, provenance.observed_at, age)) {
    // An unrepresentable distance means the evidence cannot be shown fresh.
    return false;
  }
  if (age.nanos < 0) {
    // Evidence dated after the evaluation instant is not usable: the clock
    // cannot vouch for it.
    return false;
  }
  return age.nanos <= provenance.valid_for.nanos;
}

Duration age_of(const Provenance& provenance, Timestamp now) noexcept {
  Duration age{};
  if (!elapsed(now, provenance.observed_at, age)) {
    return Duration{};
  }
  if (age.nanos < 0) {
    return Duration{};
  }
  return age;
}

Provenance observed_provenance(EvidenceSourceId source_id, ProvenanceSource source, Timestamp observed_at,
                               Duration valid_for) {
  Provenance result;
  result.evidence_class = EvidenceClass::Measured;
  result.source = source;
  result.source_id = source_id;
  result.observed_at = observed_at;
  result.valid_for = valid_for;
  result.sequence = Sequence::initial();
  result.authoritative = true;
  return result;
}

Provenance declared_provenance(EvidenceSourceId source_id, Timestamp declared_at, Duration valid_for) {
  Provenance result;
  result.evidence_class = EvidenceClass::Declared;
  result.source = ProvenanceSource::Operator;
  result.source_id = source_id;
  result.observed_at = declared_at;
  result.valid_for = valid_for;
  result.sequence = Sequence::initial();
  result.authoritative = true;
  return result;
}

Provenance predicted_provenance(EvidenceSourceId source_id, Timestamp produced_at, FabricEpoch epoch) {
  Provenance result;
  result.evidence_class = EvidenceClass::Predicted;
  result.source = ProvenanceSource::Computation;
  result.source_id = source_id;
  result.observed_at = produced_at;
  result.valid_for = Duration{};
  result.sequence = Sequence::initial();
  result.epoch = epoch;
  result.authoritative = false;
  return result;
}

Provenance derived_provenance(EvidenceSourceId source_id, Timestamp produced_at) {
  Provenance result;
  result.evidence_class = EvidenceClass::Derived;
  result.source = ProvenanceSource::Computation;
  result.source_id = source_id;
  result.observed_at = produced_at;
  result.valid_for = Duration{};
  result.sequence = Sequence::initial();
  result.authoritative = false;
  return result;
}

}  // namespace cfn