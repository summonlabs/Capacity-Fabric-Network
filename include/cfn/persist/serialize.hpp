// Capacity Fabric Network - bounded binary serialisation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The durable and wire encodings are little endian, length prefixed, and
// strictly decoded: every read checks the remaining length first, and a
// decoder that runs out of bytes fails instead of returning partial data.
#ifndef CFN_PERSIST_SERIALIZE_HPP
#define CFN_PERSIST_SERIALIZE_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/engine/snapshot.hpp"
#include "cfn/persist/records.hpp"
#include "cfn/model/capacity_model.hpp"
#include "cfn/model/degradation.hpp"
#include "cfn/model/demand_shape.hpp"
#include "cfn/model/policy.hpp"
#include "cfn/model/reservation.hpp"
#include "cfn/model/resource.hpp"
#include "cfn/model/topology.hpp"

namespace cfn::serialize {

class CFN_API Writer {
 public:
  void u8(std::uint8_t value);
  void u16(std::uint16_t value);
  void u32(std::uint32_t value);
  void u64(std::uint64_t value);
  void i64(std::int64_t value);
  void boolean(bool value);
  void text(std::string_view value);
  void bytes(std::span<const std::byte> value);

  void capacity(Capacity value) { u64(value.units); }
  void generation(Generation value) { u64(value.value()); }
  void epoch(FabricEpoch value) { u64(value.value()); }
  void boot(BootIncarnation value) { u64(value.value()); }
  void sequence(Sequence value) { u64(value.value()); }
  void duration(Duration value) { i64(value.nanos); }
  void timestamp(Timestamp value) { i64(value.unix_nanos); }
  void instant(Instant value) { i64(value.nanos); }
  void provenance(const Provenance& value);

  template <class Id>
  void id(const Id& value) {
    u8(static_cast<std::uint8_t>(value.size()));
    bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(value.view().data()), value.size()));
  }

  [[nodiscard]] const std::vector<std::byte>& data() const noexcept { return data_; }
  [[nodiscard]] std::vector<std::byte> take() { return std::move(data_); }
  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

 private:
  std::vector<std::byte> data_;
};

class CFN_API Reader {
 public:
  Reader(const std::byte* data, std::size_t size) noexcept : data_(data), size_(size) {}
  explicit Reader(std::span<const std::byte> data) noexcept : data_(data.data()), size_(data.size()) {}

  [[nodiscard]] bool u8(std::uint8_t& out) noexcept;
  [[nodiscard]] bool u16(std::uint16_t& out) noexcept;
  [[nodiscard]] bool u32(std::uint32_t& out) noexcept;
  [[nodiscard]] bool u64(std::uint64_t& out) noexcept;
  [[nodiscard]] bool i64(std::int64_t& out) noexcept;
  [[nodiscard]] bool boolean(bool& out) noexcept;
  [[nodiscard]] bool text(std::string& out, std::size_t max_bytes);
  [[nodiscard]] bool bytes(std::span<std::byte> out) noexcept;
  [[nodiscard]] bool skip(std::size_t count) noexcept;

  [[nodiscard]] bool capacity(Capacity& out) noexcept;
  [[nodiscard]] bool generation(Generation& out) noexcept;
  [[nodiscard]] bool epoch(FabricEpoch& out) noexcept;
  [[nodiscard]] bool boot(BootIncarnation& out) noexcept;
  [[nodiscard]] bool sequence(Sequence& out) noexcept;
  [[nodiscard]] bool duration(Duration& out) noexcept;
  [[nodiscard]] bool timestamp(Timestamp& out) noexcept;
  [[nodiscard]] bool instant(Instant& out) noexcept;
  [[nodiscard]] bool provenance(Provenance& out);

  template <class Id>
  [[nodiscard]] bool id(Id& out) {
    std::uint8_t length = 0;
    if (!u8(length)) {
      return false;
    }
    if (length > kMaxIdentityBytes || length > remaining()) {
      failed_ = true;
      return false;
    }
    if (length == 0) {
      // The null identity is a legitimate value: it means "not bound".
      out = Id{};
      return true;
    }
    const auto parsed = Id::parse(std::string_view(reinterpret_cast<const char*>(data_ + offset_), length));
    offset_ += length;
    if (!parsed.has_value()) {
      failed_ = true;
      return false;
    }
    out = *parsed;
    return true;
  }

  [[nodiscard]] bool failed() const noexcept { return failed_; }
  [[nodiscard]] bool at_end() const noexcept { return offset_ == size_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }
  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

 private:
  const std::byte* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t offset_ = 0;
  bool failed_ = false;
};

// --- shared value encoders --------------------------------------------------
CFN_API void encode(Writer& writer, const Provenance& value);
CFN_API Outcome<void> decode(Reader& reader, Provenance& value);
CFN_API void encode(Writer& writer, const GenerationVector& value);
CFN_API Outcome<void> decode(Reader& reader, GenerationVector& value);
CFN_API void encode(Writer& writer, const AccountingRollup& value);
CFN_API Outcome<void> decode(Reader& reader, AccountingRollup& value);

CFN_API void encode(Writer& writer, const CapacityModel& value);
CFN_API Outcome<void> decode(Reader& reader, CapacityModel& value);
CFN_API void encode(Writer& writer, const CapacityPolicy& value);
CFN_API Outcome<void> decode(Reader& reader, CapacityPolicy& value);
CFN_API void encode(Writer& writer, const DemandShape& value);
CFN_API Outcome<void> decode(Reader& reader, DemandShape& value);
CFN_API void encode(Writer& writer, const SnapshotRecord& value);
CFN_API Outcome<void> decode(Reader& reader, SnapshotRecord& value);
CFN_API void encode(Writer& writer, const EvidenceReference& value);
CFN_API Outcome<void> decode(Reader& reader, EvidenceReference& value);
CFN_API void encode(Writer& writer, const LiveAuthorityRecord& value);
CFN_API Outcome<void> decode(Reader& reader, LiveAuthorityRecord& value);

}  // namespace cfn::serialize

#endif  // CFN_PERSIST_SERIALIZE_HPP