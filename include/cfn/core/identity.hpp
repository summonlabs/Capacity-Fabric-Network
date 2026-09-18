// Capacity Fabric Network - strongly typed identities.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Every identity in the model is a distinct type. Two identities of different
// kinds cannot be compared, assigned, or passed interchangeably, so an
// accidental mix-up between (for example) a resource and a failure domain is a
// compile error rather than a silent modelling defect.
#ifndef CFN_CORE_IDENTITY_HPP
#define CFN_CORE_IDENTITY_HPP

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "cfn/core/api.hpp"

namespace cfn {

/// Identity namespaces. The value is part of the persistence format.
enum class IdKind : std::uint8_t {
  Generic = 0,
  Resource = 1,
  FailureDomain = 2,
  TopologyEdge = 3,
  Reservation = 4,
  CapacityModel = 5,
  CapacitySnapshot = 6,
  DemandShape = 7,
  Flow = 8,
  Policy = 9,
  EvidenceSource = 10,
  Publisher = 11,
  Worker = 12,
  Lease = 13,
  Attempt = 14,
  Transaction = 15,
};

CFN_API std::string_view to_string(IdKind kind) noexcept;

/// Maximum accepted identity length, in bytes.
inline constexpr std::size_t kMaxIdentityBytes = 64;

/// Returns true when the text is a structurally valid identity: 1..64 bytes,
/// printable US-ASCII from the permitted set. Whitespace, control characters,
/// and non-ASCII bytes are rejected so identities stay canonical.
CFN_API bool is_valid_identity_text(std::string_view text) noexcept;

template <IdKind Kind>
class BasicId {
 public:
  using kind_type = std::integral_constant<IdKind, Kind>;
  static constexpr IdKind kind = Kind;

  constexpr BasicId() noexcept = default;

  /// Parses and validates. Returns nullopt for structurally invalid text.
  [[nodiscard]] static std::optional<BasicId> parse(std::string_view text) noexcept {
    if (!is_valid_identity_text(text)) {
      return std::nullopt;
    }
    BasicId result;
    result.assign_unchecked(text);
    return result;
  }

  /// Builds a deterministic identity from a prefix and an unsigned counter.
  [[nodiscard]] static BasicId sequential(std::string_view prefix, std::uint64_t ordinal) {
    std::string text(prefix);
    text.push_back('-');
    text.append(format_ordinal(ordinal));
    if (text.size() > kMaxIdentityBytes) {
      text.resize(kMaxIdentityBytes);
    }
    BasicId result;
    result.assign_unchecked(text);
    return result;
  }

  [[nodiscard]] constexpr bool valid() const noexcept { return size_ != 0; }
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return std::string_view(bytes_.data(), size_);
  }
  [[nodiscard]] constexpr const char* c_str() const noexcept { return bytes_.data(); }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

  [[nodiscard]] std::string str() const { return std::string(view()); }

  [[nodiscard]] constexpr std::uint64_t hash() const noexcept {
    std::uint64_t value = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size_; ++index) {
      value ^= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[index]));
      value *= 1099511628211ULL;
    }
    return value;
  }

  friend constexpr bool operator==(const BasicId& lhs, const BasicId& rhs) noexcept {
    return lhs.view() == rhs.view();
  }
  friend constexpr std::strong_ordering operator<=>(const BasicId& lhs, const BasicId& rhs) noexcept {
    return lhs.view() <=> rhs.view();
  }

 private:
  static std::string format_ordinal(std::uint64_t ordinal);

  constexpr void assign_unchecked(std::string_view text) noexcept {
    bytes_.fill('\0');
    for (std::size_t index = 0; index < text.size(); ++index) {
      bytes_[index] = text[index];
    }
    size_ = static_cast<std::uint32_t>(text.size());
  }

  std::array<char, kMaxIdentityBytes + 1> bytes_{};
  std::uint32_t size_ = 0;
};

using GenericId = BasicId<IdKind::Generic>;
using ResourceId = BasicId<IdKind::Resource>;
using FailureDomainId = BasicId<IdKind::FailureDomain>;
using TopologyEdgeId = BasicId<IdKind::TopologyEdge>;
using ReservationId = BasicId<IdKind::Reservation>;
using CapacityModelId = BasicId<IdKind::CapacityModel>;
using CapacitySnapshotId = BasicId<IdKind::CapacitySnapshot>;
using DemandShapeId = BasicId<IdKind::DemandShape>;
using FlowId = BasicId<IdKind::Flow>;
using PolicyId = BasicId<IdKind::Policy>;
using EvidenceSourceId = BasicId<IdKind::EvidenceSource>;
using PublisherId = BasicId<IdKind::Publisher>;
using WorkerId = BasicId<IdKind::Worker>;
using LeaseId = BasicId<IdKind::Lease>;
using AttemptId = BasicId<IdKind::Attempt>;
using TransactionId = BasicId<IdKind::Transaction>;

/// Monotonic counters: generations, epochs, incarnations, and attempts.
/// Value zero always means "not yet established" and never counts as evidence.
template <class Tag>
class Monotonic {
 public:
  using value_type = std::uint64_t;

  constexpr Monotonic() noexcept = default;

  [[nodiscard]] static constexpr Monotonic from_value(std::uint64_t value) noexcept {
    Monotonic result;
    result.value_ = value;
    return result;
  }
  [[nodiscard]] static constexpr Monotonic initial() noexcept { return from_value(1); }

  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
  [[nodiscard]] constexpr bool is_newer_than(Monotonic other) const noexcept {
    return value_ > other.value_;
  }
  [[nodiscard]] constexpr bool is_older_than(Monotonic other) const noexcept {
    return value_ < other.value_;
  }

  /// Returns nullopt when the counter is exhausted rather than wrapping.
  [[nodiscard]] constexpr std::optional<Monotonic> try_next() const noexcept {
    if (value_ == UINT64_MAX) {
      return std::nullopt;
    }
    return from_value(value_ + 1);
  }

  friend constexpr bool operator==(Monotonic, Monotonic) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(Monotonic, Monotonic) noexcept = default;

 private:
  std::uint64_t value_ = 0;
};

struct GenerationTag {};
struct FabricEpochTag {};
struct BootIncarnationTag {};
struct AttemptOrdinalTag {};
struct SequenceTag {};

/// Generation of an authoritative artefact (topology, policy, resource, ...).
using Generation = Monotonic<GenerationTag>;
/// Fabric epoch: advances on every durable store boot that can mutate state.
using FabricEpoch = Monotonic<FabricEpochTag>;
/// Process incarnation within a fabric epoch.
using BootIncarnation = Monotonic<BootIncarnationTag>;
using AttemptOrdinal = Monotonic<AttemptOrdinalTag>;
using Sequence = Monotonic<SequenceTag>;

/// A 64-bit digest that binds a decision to an exact input set.
struct Digest {
  std::uint64_t value = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(Digest, Digest) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(Digest, Digest) noexcept = default;
};

}  // namespace cfn

#endif  // CFN_CORE_IDENTITY_HPP
