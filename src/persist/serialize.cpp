// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/persist/serialize.hpp"

#include <array>
#include <cstring>

namespace cfn::serialize {

namespace {

constexpr std::size_t kMaxTextBytes = 4096;

}  // namespace

void Writer::u8(std::uint8_t value) { data_.push_back(static_cast<std::byte>(value)); }

void Writer::u16(std::uint16_t value) {
  for (std::size_t index = 0; index < 2; ++index) {
    data_.push_back(static_cast<std::byte>((value >> (8U * index)) & 0xFFU));
  }
}

void Writer::u32(std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    data_.push_back(static_cast<std::byte>((value >> (8U * index)) & 0xFFU));
  }
}

void Writer::u64(std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    data_.push_back(static_cast<std::byte>((value >> (8U * index)) & 0xFFU));
  }
}

void Writer::i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }

void Writer::boolean(bool value) { u8(value ? 1U : 0U); }

void Writer::text(std::string_view value) {
  const std::uint32_t size = static_cast<std::uint32_t>(value.size());
  u32(size);
  bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(value.data()), value.size()));
}

void Writer::bytes(std::span<const std::byte> value) { data_.insert(data_.end(), value.begin(), value.end()); }

void Writer::provenance(const Provenance& value) {
  u8(static_cast<std::uint8_t>(value.evidence_class));
  u8(static_cast<std::uint8_t>(value.source));
  id(value.source_id);
  timestamp(value.observed_at);
  duration(value.valid_for);
  sequence(value.sequence);
  epoch(value.epoch);
  boolean(value.authoritative);
}

bool Reader::u8(std::uint8_t& out) noexcept {
  if (remaining() < 1) {
    failed_ = true;
    return false;
  }
  out = static_cast<std::uint8_t>(data_[offset_]);
  offset_ += 1;
  return true;
}

bool Reader::u16(std::uint16_t& out) noexcept {
  if (remaining() < 2) {
    failed_ = true;
    return false;
  }
  std::uint16_t value = 0;
  for (std::size_t index = 0; index < 2; ++index) {
    value |= static_cast<std::uint16_t>(static_cast<std::uint8_t>(data_[offset_ + index])) << (8U * index);
  }
  offset_ += 2;
  out = value;
  return true;
}

bool Reader::u32(std::uint32_t& out) noexcept {
  if (remaining() < 4) {
    failed_ = true;
    return false;
  }
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[offset_ + index])) << (8U * index);
  }
  offset_ += 4;
  out = value;
  return true;
}

bool Reader::u64(std::uint64_t& out) noexcept {
  if (remaining() < 8) {
    failed_ = true;
    return false;
  }
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[offset_ + index])) << (8U * index);
  }
  offset_ += 8;
  out = value;
  return true;
}

bool Reader::i64(std::int64_t& out) noexcept {
  std::uint64_t raw = 0;
  if (!u64(raw)) {
    return false;
  }
  out = static_cast<std::int64_t>(raw);
  return true;
}

bool Reader::boolean(bool& out) noexcept {
  std::uint8_t raw = 0;
  if (!u8(raw)) {
    return false;
  }
  out = raw != 0U;
  return true;
}

bool Reader::text(std::string& out, std::size_t max_bytes) {
  std::uint32_t size = 0;
  if (!u32(size)) {
    return false;
  }
  if (size > max_bytes || remaining() < size) {
    failed_ = true;
    return false;
  }
  out.assign(reinterpret_cast<const char*>(data_ + offset_), size);
  offset_ += size;
  return true;
}

bool Reader::bytes(std::span<std::byte> out) noexcept {
  if (remaining() < out.size()) {
    failed_ = true;
    return false;
  }
  if (!out.empty()) {
    std::memcpy(out.data(), data_ + offset_, out.size());
  }
  offset_ += out.size();
  return true;
}

bool Reader::skip(std::size_t count) noexcept {
  if (remaining() < count) {
    failed_ = true;
    return false;
  }
  offset_ += count;
  return true;
}

bool Reader::capacity(Capacity& out) noexcept {
  std::uint64_t value = 0;
  if (!u64(value)) {
    return false;
  }
  out = Capacity::from_units(value);
  return true;
}

bool Reader::generation(Generation& out) noexcept {
  std::uint64_t value = 0;
  if (!u64(value)) {
    return false;
  }
  out = Generation::from_value(value);
  return true;
}

bool Reader::epoch(FabricEpoch& out) noexcept {
  std::uint64_t value = 0;
  if (!u64(value)) {
    return false;
  }
  out = FabricEpoch::from_value(value);
  return true;
}

bool Reader::boot(BootIncarnation& out) noexcept {
  std::uint64_t value = 0;
  if (!u64(value)) {
    return false;
  }
  out = BootIncarnation::from_value(value);
  return true;
}

bool Reader::sequence(Sequence& out) noexcept {
  std::uint64_t value = 0;
  if (!u64(value)) {
    return false;
  }
  out = Sequence::from_value(value);
  return true;
}

bool Reader::duration(Duration& out) noexcept {
  std::int64_t value = 0;
  if (!i64(value)) {
    return false;
  }
  out = Duration::from_nanos(value);
  return true;
}

bool Reader::timestamp(Timestamp& out) noexcept {
  std::int64_t value = 0;
  if (!i64(value)) {
    return false;
  }
  out = Timestamp::from_unix_nanos(value);
  return true;
}

bool Reader::instant(Instant& out) noexcept {
  std::int64_t value = 0;
  if (!i64(value)) {
    return false;
  }
  out = Instant{value};
  return true;
}

bool Reader::provenance(Provenance& out) {
  std::uint8_t evidence = 0;
  std::uint8_t source = 0;
  if (!u8(evidence) || !u8(source)) {
    return false;
  }
  if (evidence > static_cast<std::uint8_t>(EvidenceClass::Imported) ||
      source > static_cast<std::uint8_t>(ProvenanceSource::SyntheticGenerator)) {
    failed_ = true;
    return false;
  }
  out.evidence_class = static_cast<EvidenceClass>(evidence);
  out.source = static_cast<ProvenanceSource>(source);
  return id(out.source_id) && timestamp(out.observed_at) && duration(out.valid_for) &&
         sequence(out.sequence) && epoch(out.epoch) && boolean(out.authoritative);
}

void encode(Writer& writer, const Provenance& value) { writer.provenance(value); }

Outcome<void> decode(Reader& reader, Provenance& value) {
  if (!reader.provenance(value)) {
    return Error(ErrorCode::Truncated, "provenance record is malformed");
  }
  return Outcome<void>();
}

void encode(Writer& writer, const GenerationVector& value) {
  writer.epoch(value.fabric_epoch);
  writer.generation(value.model);
  writer.generation(value.policy);
  writer.generation(value.topology);
  writer.generation(value.resource_catalog);
  writer.generation(value.reservation_snapshot);
  writer.generation(value.failure_domain_catalog);
  writer.generation(value.degradation);
  writer.generation(value.demand_shape);
  writer.u64(value.resource_set.value);
  writer.u32(value.resource_count);
  writer.timestamp(value.computed_at);
  writer.instant(value.computed_monotonic);
  writer.provenance(value.provenance);
}

Outcome<void> decode(Reader& reader, GenerationVector& value) {
  bool ok = reader.epoch(value.fabric_epoch) && reader.generation(value.model) &&
            reader.generation(value.policy) && reader.generation(value.topology) &&
            reader.generation(value.resource_catalog) &&
            reader.generation(value.reservation_snapshot) &&
            reader.generation(value.failure_domain_catalog) && reader.generation(value.degradation) &&
            reader.generation(value.demand_shape) && reader.u64(value.resource_set.value) &&
            reader.u32(value.resource_count) && reader.timestamp(value.computed_at) &&
            reader.instant(value.computed_monotonic) && reader.provenance(value.provenance);
  if (!ok) {
    return Error(ErrorCode::Truncated, "generation vector is malformed");
  }
  return Outcome<void>();
}

void encode(Writer& writer, const AccountingRollup& value) {
  writer.capacity(value.raw_total);
  writer.capacity(value.unknown_total);
  writer.capacity(value.reserved_total);
  writer.capacity(value.headroom_total);
  writer.capacity(value.degraded_total);
  writer.capacity(value.available_total);
  writer.capacity(value.usable_total);
  writer.capacity(value.spare_total);
  writer.capacity(value.stranded_total);
  writer.capacity(value.stranding.segmentation);
  writer.capacity(value.stranding.bottleneck);
  writer.capacity(value.stranding.failure_domain_resilience);
  writer.capacity(value.reserved_stranded);
  writer.boolean(value.fragmentation_evaluated);
  writer.u32(value.resource_count);
  writer.u32(value.present_resource_count);
  writer.u32(value.authoritative_resource_count);
  writer.u32(value.unknown_resource_count);
}

Outcome<void> decode(Reader& reader, AccountingRollup& value) {
  bool ok = reader.capacity(value.raw_total) && reader.capacity(value.unknown_total) &&
            reader.capacity(value.reserved_total) && reader.capacity(value.headroom_total) &&
            reader.capacity(value.degraded_total) && reader.capacity(value.available_total) &&
            reader.capacity(value.usable_total) && reader.capacity(value.spare_total) &&
            reader.capacity(value.stranded_total) && reader.capacity(value.stranding.segmentation) &&
            reader.capacity(value.stranding.bottleneck) &&
            reader.capacity(value.stranding.failure_domain_resilience) &&
            reader.capacity(value.reserved_stranded) && reader.boolean(value.fragmentation_evaluated) &&
            reader.u32(value.resource_count) && reader.u32(value.present_resource_count) &&
            reader.u32(value.authoritative_resource_count) && reader.u32(value.unknown_resource_count);
  if (!ok) {
    return Error(ErrorCode::Truncated, "accounting rollup is malformed");
  }
  return Outcome<void>();
}

void encode(Writer& writer, const CapacityModel& value) {
  writer.id(value.id);
  writer.generation(value.generation);
  writer.epoch(value.epoch);
  writer.id(value.policy);
  writer.generation(value.policy_generation);
  writer.id(value.demand_shape);
  writer.generation(value.demand_shape_generation);
  writer.text(value.label.view());
  writer.timestamp(value.created_at);
  writer.provenance(value.provenance);
}

Outcome<void> decode(Reader& reader, CapacityModel& value) {
  std::string label;
  const bool ok = reader.id(value.id) && reader.generation(value.generation) &&
                  reader.epoch(value.epoch) && reader.id(value.policy) &&
                  reader.generation(value.policy_generation) && reader.id(value.demand_shape) &&
                  reader.generation(value.demand_shape_generation) &&
                  reader.text(label, decltype(value.label)::capacity) &&
                  reader.timestamp(value.created_at) && reader.provenance(value.provenance);
  if (!ok) {
    return Error(ErrorCode::Truncated, "capacity model record is malformed");
  }
  if (!value.label.assign(label)) {
    return Error(ErrorCode::LimitExceeded, "capacity model label exceeds its bound");
  }
  return Outcome<void>();
}

void encode(Writer& writer, const CapacityPolicy& value) {
  writer.id(value.id);
  writer.generation(value.generation);
  writer.epoch(value.epoch);
  writer.u32(value.headroom_floor_ppm);
  writer.capacity(value.headroom_floor_absolute);
  writer.u8(static_cast<std::uint8_t>(value.resilience));
  writer.boolean(value.reject_unknown_capacity);
  writer.boolean(value.require_single_slice);
  writer.provenance(value.provenance);
}

Outcome<void> decode(Reader& reader, CapacityPolicy& value) {
  std::uint8_t resilience = 0;
  const bool ok = reader.id(value.id) && reader.generation(value.generation) &&
                  reader.epoch(value.epoch) && reader.u32(value.headroom_floor_ppm) &&
                  reader.capacity(value.headroom_floor_absolute) && reader.u8(resilience) &&
                  reader.boolean(value.reject_unknown_capacity) &&
                  reader.boolean(value.require_single_slice) && reader.provenance(value.provenance);
  if (!ok) {
    return Error(ErrorCode::Truncated, "policy record is malformed");
  }
  if (resilience > static_cast<std::uint8_t>(ResilienceMode::DomainN1N1)) {
    return Error(ErrorCode::MalformedInput, "policy record declares an unknown resilience mode");
  }
  value.resilience = static_cast<ResilienceMode>(resilience);
  return Outcome<void>();
}

void encode(Writer& writer, const DemandShape& value) {
  writer.id(value.id);
  writer.generation(value.generation);
  writer.epoch(value.epoch);
  writer.provenance(value.provenance);
  writer.capacity(value.granularity);
  writer.u32(static_cast<std::uint32_t>(value.flows.size()));
  for (const FlowDemand& flow : value.flows) {
    writer.id(flow.id);
    writer.id(flow.source);
    writer.id(flow.sink);
    writer.capacity(flow.magnitude);
    writer.u32(flow.priority);
  }
}

Outcome<void> decode(Reader& reader, DemandShape& value) {
  if (!reader.id(value.id) || !reader.generation(value.generation) || !reader.epoch(value.epoch) ||
      !reader.provenance(value.provenance) || !reader.capacity(value.granularity)) {
    return Error(ErrorCode::Truncated, "demand shape record is malformed");
  }
  std::uint32_t count = 0;
  if (!reader.u32(count)) {
    return Error(ErrorCode::Truncated, "demand shape record is malformed");
  }
  if (count == 0 || count > 1000000U) {
    return Error(ErrorCode::MalformedInput, "demand shape declares an impossible flow count");
  }
  value.flows.clear();
  value.flows.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    FlowDemand flow;
    if (!reader.id(flow.id) || !reader.id(flow.source) || !reader.id(flow.sink) ||
        !reader.capacity(flow.magnitude) || !reader.u32(flow.priority)) {
      return Error(ErrorCode::Truncated, "demand flow record is malformed");
    }
    value.flows.push_back(flow);
  }
  return Outcome<void>();
}

void encode(Writer& writer, const SnapshotRecord& value) {
  writer.id(value.id);
  writer.generation(value.generation);
  writer.id(value.model);
  encode(writer, value.generations);
  encode(writer, value.rollup);
  writer.timestamp(value.recorded_at);
}

Outcome<void> decode(Reader& reader, SnapshotRecord& value) {
  if (!reader.id(value.id) || !reader.generation(value.generation) || !reader.id(value.model)) {
    return Error(ErrorCode::Truncated, "snapshot record is malformed");
  }
  CFN_RETURN_IF_ERROR(decode(reader, value.generations));
  CFN_RETURN_IF_ERROR(decode(reader, value.rollup));
  if (!reader.timestamp(value.recorded_at)) {
    return Error(ErrorCode::Truncated, "snapshot record is malformed");
  }
  return Outcome<void>();
}

void encode(Writer& writer, const EvidenceReference& value) {
  writer.id(value.source);
  writer.generation(value.generation);
  writer.timestamp(value.last_observed_at);
  writer.duration(value.valid_for);
  writer.text(value.locator.view());
  writer.boolean(value.requires_revalidation);
}

Outcome<void> decode(Reader& reader, EvidenceReference& value) {
  std::string locator;
  const bool ok = reader.id(value.source) && reader.generation(value.generation) &&
                  reader.timestamp(value.last_observed_at) && reader.duration(value.valid_for) &&
                  reader.text(locator, decltype(value.locator)::capacity) &&
                  reader.boolean(value.requires_revalidation);
  if (!ok) {
    return Error(ErrorCode::Truncated, "evidence reference record is malformed");
  }
  if (!value.locator.assign(locator)) {
    return Error(ErrorCode::LimitExceeded, "evidence locator exceeds its bound");
  }
  return Outcome<void>();
}

void encode(Writer& writer, const LiveAuthorityRecord& value) {
  writer.u8(static_cast<std::uint8_t>(value.kind));
  writer.id(value.holder);
  writer.epoch(value.epoch);
  writer.boot(value.boot);
  writer.timestamp(value.granted_at);
  writer.duration(value.ttl);
  writer.sequence(value.sequence);
}

Outcome<void> decode(Reader& reader, LiveAuthorityRecord& value) {
  std::uint8_t kind = 0;
  const bool ok = reader.u8(kind) && reader.id(value.holder) && reader.epoch(value.epoch) &&
                  reader.boot(value.boot) && reader.timestamp(value.granted_at) &&
                  reader.duration(value.ttl) && reader.sequence(value.sequence);
  if (!ok) {
    return Error(ErrorCode::Truncated, "live authority record is malformed");
  }
  if (kind > static_cast<std::uint8_t>(LiveAuthorityKind::TelemetryFreshness)) {
    return Error(ErrorCode::MalformedInput, "live authority record declares an unknown kind");
  }
  value.kind = static_cast<LiveAuthorityKind>(kind);
  return Outcome<void>();
}

}  // namespace cfn::serialize