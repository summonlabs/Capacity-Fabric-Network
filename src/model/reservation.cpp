// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/model/reservation.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <vector>

#include "cfn/core/checked.hpp"

namespace cfn {

std::string_view to_string(ReservationClass value) noexcept {
  switch (value) {
    case ReservationClass::Unknown: return "unknown";
    case ReservationClass::Committed: return "committed";
    case ReservationClass::Protected: return "protected";
    case ReservationClass::Pinned: return "pinned";
    case ReservationClass::BestEffort: return "best-effort";
  }
  return "unknown";
}

bool reservation_class_from_string(std::string_view text, ReservationClass& out) noexcept {
  if (text == "unknown") { out = ReservationClass::Unknown; return true; }
  if (text == "committed") { out = ReservationClass::Committed; return true; }
  if (text == "protected") { out = ReservationClass::Protected; return true; }
  if (text == "pinned") { out = ReservationClass::Pinned; return true; }
  if (text == "best-effort") { out = ReservationClass::BestEffort; return true; }
  return false;
}

const Reservation* ReservationSnapshot::find(const ReservationId& id) const noexcept {
  for (const Reservation& reservation : reservations) {
    if (reservation.id == id) {
      return &reservation;
    }
  }
  return nullptr;
}

Outcome<void> validate(const ReservationSnapshot& snapshot, const ResourceCatalog& catalog,
                       const Limits& limits) {
  if (!snapshot.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "reservation snapshot generation is not established");
  }
  if (snapshot.reservations.size() > limits.max_reservations) {
    return Error(ErrorCode::LimitExceeded, "reservation population exceeds the configured maximum");
  }
  CFN_RETURN_IF_ERROR(validate(snapshot.provenance));

  std::map<std::string_view, const ResourceRecord*> catalog_index;
  for (const ResourceRecord& record : catalog.resources) {
    catalog_index.emplace(record.id.view(), &record);
  }

  std::map<ResourceId, std::uint64_t> totals;
  std::vector<std::string_view> identities;
  identities.reserve(snapshot.reservations.size());

  for (const Reservation& reservation : snapshot.reservations) {
    if (!reservation.id.valid()) {
      return Error(ErrorCode::InvalidArgument, "reservation identity is empty");
    }
    if (!reservation.generation.valid()) {
      return Error(ErrorCode::InvalidArgument, "reservation generation is not established",
                   reservation.id.view());
    }
    if (reservation.amount.is_zero()) {
      return Error(ErrorCode::InvalidArgument, "reservation amount must be positive",
                   reservation.id.view());
    }
    if (reservation.amount.units > limits.max_capacity_units) {
      return Error(ErrorCode::LimitExceeded, "reservation amount exceeds the configured maximum",
                   reservation.id.view());
    }
    if (reservation.priority > limits.max_priority) {
      return Error(ErrorCode::LimitExceeded, "reservation priority exceeds the configured maximum",
                   reservation.id.view());
    }
    const auto found = catalog_index.find(reservation.resource.view());
    if (found == catalog_index.end()) {
      return Error(ErrorCode::NotFound, "reservation names a resource that is not in the catalog",
                   reservation.resource.view());
    }
    if (!found->second->present) {
      return Error(ErrorCode::Contradictory, "reservation names a withdrawn resource",
                   reservation.resource.view());
    }
    if (!found->second->authoritative) {
      return Error(ErrorCode::NotAuthoritative,
                   "reservation names a resource without authoritative capacity",
                   reservation.resource.view());
    }
    if (!reservation.resource_generation.valid() ||
        reservation.resource_generation != found->second->generation) {
      return Error(ErrorCode::StaleReservation,
                   "reservation is bound to a resource generation that is no longer current",
                   reservation.resource.view());
    }
    CFN_RETURN_IF_ERROR_CTX(validate(reservation.provenance), reservation.id.view());

    std::uint64_t& total = totals[reservation.resource];
    std::uint64_t next = 0;
    if (!checked::add_u64(total, reservation.amount.units, next)) {
      return Error(ErrorCode::Overflow, "reserved capacity total overflows", reservation.resource.view());
    }
    total = next;
    identities.push_back(reservation.id.view());
  }

  for (const auto& entry : totals) {
    const auto found = catalog_index.find(entry.first.view());
    if (found == catalog_index.end()) {
      continue;
    }
    if (entry.second > found->second->reported_capacity.units) {
      return Error(ErrorCode::Contradictory,
                   "reserved capacity exceeds the raw capacity of the resource", entry.first.view());
    }
  }

  std::sort(identities.begin(), identities.end());
  for (std::size_t index = 1; index < identities.size(); ++index) {
    if (identities[index] == identities[index - 1]) {
      return Error(ErrorCode::Duplicate, "duplicate reservation identity", identities[index]);
    }
  }
  return Outcome<void>();
}

}  // namespace cfn
