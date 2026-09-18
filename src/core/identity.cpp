// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/core/identity.hpp"

namespace cfn {

std::string_view to_string(IdKind kind) noexcept {
  switch (kind) {
    case IdKind::Generic: return "generic";
    case IdKind::Resource: return "resource";
    case IdKind::FailureDomain: return "failure-domain";
    case IdKind::TopologyEdge: return "topology-edge";
    case IdKind::Reservation: return "reservation";
    case IdKind::CapacityModel: return "capacity-model";
    case IdKind::CapacitySnapshot: return "capacity-snapshot";
    case IdKind::DemandShape: return "demand-shape";
    case IdKind::Flow: return "flow";
    case IdKind::Policy: return "policy";
    case IdKind::EvidenceSource: return "evidence-source";
    case IdKind::Publisher: return "publisher";
    case IdKind::Worker: return "worker";
    case IdKind::Lease: return "lease";
    case IdKind::Attempt: return "attempt";
    case IdKind::Transaction: return "transaction";
  }
  return "unknown";
}

namespace {

constexpr bool is_permitted_identity_character(char value) noexcept {
  if (value >= 'A' && value <= 'Z') {
    return true;
  }
  if (value >= 'a' && value <= 'z') {
    return true;
  }
  if (value >= '0' && value <= '9') {
    return true;
  }
  switch (value) {
    case '-':
    case '_':
    case '.':
    case ':':
    case '@':
    case '+':
    case '/':
    case '#':
      return true;
    default:
      return false;
  }
}

}  // namespace

bool is_valid_identity_text(std::string_view text) noexcept {
  if (text.empty() || text.size() > kMaxIdentityBytes) {
    return false;
  }
  for (const char character : text) {
    if (!is_permitted_identity_character(character)) {
      return false;
    }
  }
  return true;
}

template <IdKind Kind>
std::string BasicId<Kind>::format_ordinal(std::uint64_t ordinal) {
  char buffer[24] = {};
  std::size_t index = sizeof(buffer);
  buffer[--index] = '\0';
  if (ordinal == 0) {
    buffer[--index] = '0';
  }
  while (ordinal != 0 && index > 0) {
    buffer[--index] = static_cast<char>('0' + static_cast<int>(ordinal % 10));
    ordinal /= 10;
  }
  // Left pad with zeros so identities sort in creation order and stay a fixed
  // width regardless of the counter value.
  std::string digits(&buffer[index]);
  std::string padded;
  if (digits.size() < 16) {
    padded.append(16 - digits.size(), '0');
  }
  padded.append(digits);
  return padded;
}

template class BasicId<IdKind::Generic>;
template class BasicId<IdKind::Resource>;
template class BasicId<IdKind::FailureDomain>;
template class BasicId<IdKind::TopologyEdge>;
template class BasicId<IdKind::Reservation>;
template class BasicId<IdKind::CapacityModel>;
template class BasicId<IdKind::CapacitySnapshot>;
template class BasicId<IdKind::DemandShape>;
template class BasicId<IdKind::Flow>;
template class BasicId<IdKind::Policy>;
template class BasicId<IdKind::EvidenceSource>;
template class BasicId<IdKind::Publisher>;
template class BasicId<IdKind::Worker>;
template class BasicId<IdKind::Lease>;
template class BasicId<IdKind::Attempt>;
template class BasicId<IdKind::Transaction>;

}  // namespace cfn
