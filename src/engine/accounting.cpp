// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/engine/accounting.hpp"

#include <algorithm>
#include <map>
#include <string_view>

#include "cfn/core/checked.hpp"

namespace cfn {

std::string_view to_string(StrandCause cause) noexcept {
  switch (cause) {
    case StrandCause::None: return "none";
    case StrandCause::Segmentation: return "segmentation";
    case StrandCause::Bottleneck: return "bottleneck";
    case StrandCause::FailureDomainResilience: return "failure-domain-resilience";
  }
  return "none";
}

Capacity StrandingBreakdown::total() const noexcept {
  checked::Accumulator accumulator;
  (void)accumulator.add(segmentation.units);
  (void)accumulator.add(bottleneck.units);
  (void)accumulator.add(failure_domain_resilience.units);
  return Capacity::from_units(accumulator.value());
}

Capacity StrandingBreakdown::get(StrandCause cause) const noexcept {
  switch (cause) {
    case StrandCause::Segmentation: return segmentation;
    case StrandCause::Bottleneck: return bottleneck;
    case StrandCause::FailureDomainResilience: return failure_domain_resilience;
    case StrandCause::None: return Capacity{};
  }
  return Capacity{};
}

Outcome<std::vector<ResourceAccounting>> derive_accounting(
    const ResourceCatalog& resources, const Topology& topology, const ReservationSnapshot& reservations,
    const DegradationSnapshot& degradation, const FailureDomainCatalog& domains, const CapacityPolicy& policy,
    const Limits& limits) {
  CFN_RETURN_IF_ERROR(validate(resources, limits));
  CFN_RETURN_IF_ERROR(validate(topology, resources, limits));
  CFN_RETURN_IF_ERROR(validate(reservations, resources, limits));
  CFN_RETURN_IF_ERROR(validate(domains, limits));
  CFN_RETURN_IF_ERROR(validate(degradation, resources, domains, limits));
  CFN_RETURN_IF_ERROR(validate(policy, limits));

  std::map<std::string_view, const ResourceRecord*> catalog_index;
  for (const ResourceRecord& record : resources.resources) {
    catalog_index.emplace(record.id.view(), &record);
  }

  std::map<ResourceId, std::uint64_t> reserved_by_resource;
  for (const Reservation& reservation : reservations.reservations) {
    std::uint64_t& total = reserved_by_resource[reservation.resource];
    std::uint64_t next = 0;
    if (!checked::add_u64(total, reservation.amount.units, next)) {
      return Error(ErrorCode::Overflow, "reserved capacity total overflows", reservation.resource.view());
    }
    total = next;
  }

  std::map<ResourceId, std::uint64_t> degraded_by_resource;
  for (const DegradationRecord& record : degradation.records) {
    const auto found = catalog_index.find(record.resource.view());
    if (found == catalog_index.end()) {
      return Error(ErrorCode::NotFound, "degradation record names an unknown resource",
                   record.resource.view());
    }
    const std::uint64_t raw = found->second->authoritative ? found->second->reported_capacity.units : 0ULL;
    std::uint64_t contribution = record.lost.units;
    if (record.loss_ppm != 0) {
      if (!checked::scale_ppm(raw, record.loss_ppm, contribution)) {
        return Error(ErrorCode::Overflow, "fractional degradation overflows", record.resource.view());
      }
    }
    std::uint64_t& total = degraded_by_resource[record.resource];
    std::uint64_t next = 0;
    if (!checked::add_u64(total, contribution, next)) {
      return Error(ErrorCode::Overflow, "degradation total overflows", record.resource.view());
    }
    total = next;
  }

  std::vector<ResourceAccounting> rows;
  rows.reserve(resources.resources.size());

  for (const ResourceRecord& record : resources.resources) {
    ResourceAccounting row;
    row.resource = record.id;
    row.generation = record.generation;
    row.failure_domain = record.failure_domain;
    row.kind = record.kind;
    row.evidence_class = record.capacity_evidence;
    row.present = record.present;
    row.reported = record.reported_capacity;

    const bool usable_authority = record.authoritative && record.present;
    row.authoritative = usable_authority;
    row.raw = usable_authority ? record.reported_capacity : Capacity{};
    row.unknown = usable_authority ? Capacity{} : record.reported_capacity;

    const FailureDomainRecord* domain = nullptr;
    if (record.failure_domain.valid()) {
      domain = domains.find(record.failure_domain);
    }
    row.domain_healthy = domain == nullptr ? true : domain->healthy;

    if (!usable_authority) {
      // A resource without authoritative capacity contributes only to the
      // UNKNOWN bucket. Deductions against an unknown magnitude are meaningless
      // and are deliberately not invented.
      rows.push_back(row);
      continue;
    }

    if (policy.reject_unknown_capacity) {
      return Error(ErrorCode::NotAuthoritative,
                   "policy requires authoritative capacity for every present resource",
                   record.id.view());
    }

    if (!row.domain_healthy) {
      // The whole domain is lost: every unit of the resource is degraded, so
      // reservations and protected headroom on it are subsumed rather than
      // double counted.
      row.degraded = row.raw;
      rows.push_back(row);
      continue;
    }

    const auto reserved_entry = reserved_by_resource.find(record.id);
    const std::uint64_t reserved = reserved_entry == reserved_by_resource.end() ? 0ULL : reserved_entry->second;

    std::uint64_t headroom = policy.headroom_floor_absolute.units;
    if (policy.headroom_floor_ppm != 0) {
      std::uint64_t fractional = 0;
      if (!checked::scale_ppm(row.raw.units, policy.headroom_floor_ppm, fractional)) {
        return Error(ErrorCode::Overflow, "protected headroom overflows", record.id.view());
      }
      if (!checked::add_u64(headroom, fractional, headroom)) {
        return Error(ErrorCode::Overflow, "protected headroom overflows", record.id.view());
      }
    }

    std::uint64_t degraded = 0;
    {
      const auto degraded_entry = degraded_by_resource.find(record.id);
      if (degraded_entry != degraded_by_resource.end()) {
        degraded = degraded_entry->second;
      }
    }

    // A per-resource headroom floor can never exceed the capacity that exists
    // on that resource; a zero capacity junction simply protects nothing.
    if (headroom > row.raw.units) {
      headroom = row.raw.units;
    }

    std::uint64_t remaining = 0;
    if (!checked::sub_u64(row.raw.units, reserved, remaining)) {
      return Error(ErrorCode::Contradictory,
                   "reserved capacity exceeds the authoritative raw capacity of the resource",
                   record.id.view());
    }
    if (!checked::sub_u64(remaining, headroom, remaining)) {
      return Error(ErrorCode::Contradictory,
                   "reserved capacity plus protected headroom exceeds the raw capacity of the resource",
                   record.id.view());
    }
    if (!checked::sub_u64(remaining, degraded, remaining)) {
      return Error(ErrorCode::Contradictory,
                   "reserved capacity, protected headroom and degradation together exceed the raw "
                   "capacity of the resource",
                   record.id.view());
    }

    row.reserved = Capacity::from_units(reserved);
    row.headroom = Capacity::from_units(headroom);
    row.degraded = Capacity::from_units(degraded);
    row.available = Capacity::from_units(remaining);
    rows.push_back(row);
  }

  std::sort(rows.begin(), rows.end(), [](const ResourceAccounting& lhs, const ResourceAccounting& rhs) {
    return lhs.resource < rhs.resource;
  });
  return rows;
}

Outcome<AccountingRollup> roll_up(const std::vector<ResourceAccounting>& rows) {
  AccountingRollup rollup;
  checked::Accumulator raw;
  checked::Accumulator unknown;
  checked::Accumulator reserved;
  checked::Accumulator headroom;
  checked::Accumulator degraded;
  checked::Accumulator available;

  for (const ResourceAccounting& row : rows) {
    (void)raw.add(row.raw.units);
    (void)unknown.add(row.unknown.units);
    (void)reserved.add(row.reserved.units);
    (void)headroom.add(row.headroom.units);
    (void)degraded.add(row.degraded.units);
    (void)available.add(row.available.units);
    rollup.resource_count += 1U;
    if (row.present) {
      rollup.present_resource_count += 1U;
    }
    if (row.authoritative) {
      rollup.authoritative_resource_count += 1U;
    } else {
      rollup.unknown_resource_count += 1U;
    }
  }

  if (!raw.valid() || !unknown.valid() || !reserved.valid() || !headroom.valid() || !degraded.valid() ||
      !available.valid()) {
    return Error(ErrorCode::Overflow, "capacity totals overflow the supported range");
  }

  rollup.raw_total = Capacity::from_units(raw.value());
  rollup.unknown_total = Capacity::from_units(unknown.value());
  rollup.reserved_total = Capacity::from_units(reserved.value());
  rollup.headroom_total = Capacity::from_units(headroom.value());
  rollup.degraded_total = Capacity::from_units(degraded.value());
  rollup.available_total = Capacity::from_units(available.value());
  // Shape-independent default: with no demand shape bound there is nothing to
  // strand the capacity against.
  rollup.usable_total = rollup.available_total;
  rollup.spare_total = Capacity{};
  rollup.stranded_total = Capacity{};
  rollup.fragmentation_evaluated = false;
  return rollup;
}

Outcome<void> verify_closure(const AccountingRollup& rollup) {
  checked::Accumulator deductions;
  (void)deductions.add(rollup.reserved_total.units);
  (void)deductions.add(rollup.headroom_total.units);
  (void)deductions.add(rollup.degraded_total.units);
  (void)deductions.add(rollup.available_total.units);
  if (!deductions.valid() || deductions.value() != rollup.raw_total.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: raw capacity is not the sum of reserved, headroom, "
                 "degraded and available capacity");
  }

  checked::Accumulator distribution;
  (void)distribution.add(rollup.usable_total.units);
  (void)distribution.add(rollup.spare_total.units);
  (void)distribution.add(rollup.stranded_total.units);
  if (!distribution.valid() || distribution.value() != rollup.available_total.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: available capacity is not the sum of usable, spare and "
                 "stranded capacity");
  }

  if (rollup.stranding.total().units != rollup.stranded_total.units) {
    return Error(ErrorCode::Contradictory,
                 "accounting does not close: stranding causes do not sum to the stranded total");
  }
  return Outcome<void>();
}

}  // namespace cfn