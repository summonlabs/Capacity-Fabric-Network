// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/model/degradation.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <vector>

namespace cfn {

std::string_view to_string(DegradationCause value) noexcept {
  switch (value) {
    case DegradationCause::Unknown: return "unknown";
    case DegradationCause::Failure: return "failure";
    case DegradationCause::Maintenance: return "maintenance";
    case DegradationCause::Health: return "health";
    case DegradationCause::Thermal: return "thermal";
    case DegradationCause::ErrorRate: return "error-rate";
    case DegradationCause::Operator: return "operator";
    case DegradationCause::Spectrum: return "spectrum";
  }
  return "unknown";
}

bool degradation_cause_from_string(std::string_view text, DegradationCause& out) noexcept {
  if (text == "unknown") { out = DegradationCause::Unknown; return true; }
  if (text == "failure") { out = DegradationCause::Failure; return true; }
  if (text == "maintenance") { out = DegradationCause::Maintenance; return true; }
  if (text == "health") { out = DegradationCause::Health; return true; }
  if (text == "thermal") { out = DegradationCause::Thermal; return true; }
  if (text == "error-rate") { out = DegradationCause::ErrorRate; return true; }
  if (text == "operator") { out = DegradationCause::Operator; return true; }
  if (text == "spectrum") { out = DegradationCause::Spectrum; return true; }
  return false;
}

const FailureDomainRecord* FailureDomainCatalog::find(const FailureDomainId& id) const noexcept {
  for (const FailureDomainRecord& record : domains) {
    if (record.id == id) {
      return &record;
    }
  }
  return nullptr;
}

Outcome<void> validate(const DegradationRecord& record, const Limits& limits) {
  if (!record.resource.valid()) {
    return Error(ErrorCode::InvalidArgument, "degradation record has no resource");
  }
  if (!record.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "degradation record generation is not established",
                 record.resource.view());
  }
  if (!record.resource_generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "degradation record resource generation is not established",
                 record.resource.view());
  }
  const bool has_absolute = record.lost.units != 0;
  const bool has_fraction = record.loss_ppm != 0;
  if (has_absolute == has_fraction) {
    return Error(ErrorCode::MalformedInput,
                 "degradation record must declare exactly one of an absolute loss or a fractional loss",
                 record.resource.view());
  }
  if (record.lost.units > limits.max_capacity_units) {
    return Error(ErrorCode::LimitExceeded, "degradation loss exceeds the configured maximum",
                 record.resource.view());
  }
  if (record.loss_ppm > 1000000U) {
    return Error(ErrorCode::InvalidArgument, "degradation fraction exceeds one hundred percent",
                 record.resource.view());
  }
  CFN_RETURN_IF_ERROR(validate(record.provenance));
  return Outcome<void>();
}

Outcome<void> validate(const FailureDomainCatalog& catalog, const Limits& limits) {
  if (!catalog.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "failure domain catalog generation is not established");
  }
  if (catalog.domains.size() > limits.max_failure_domains) {
    return Error(ErrorCode::LimitExceeded, "failure domain population exceeds the configured maximum");
  }
  CFN_RETURN_IF_ERROR(validate(catalog.provenance));
  std::vector<std::string_view> identities;
  identities.reserve(catalog.domains.size());
  for (const FailureDomainRecord& record : catalog.domains) {
    if (!record.id.valid()) {
      return Error(ErrorCode::InvalidArgument, "failure domain identity is empty");
    }
    if (!record.generation.valid()) {
      return Error(ErrorCode::InvalidArgument, "failure domain generation is not established",
                   record.id.view());
    }
    CFN_RETURN_IF_ERROR_CTX(validate(record.provenance), record.id.view());
    identities.push_back(record.id.view());
  }
  std::sort(identities.begin(), identities.end());
  for (std::size_t index = 1; index < identities.size(); ++index) {
    if (identities[index] == identities[index - 1]) {
      return Error(ErrorCode::Duplicate, "duplicate failure domain identity", identities[index]);
    }
  }
  return Outcome<void>();
}

Outcome<void> validate(const DegradationSnapshot& snapshot, const ResourceCatalog& resources,
                       const FailureDomainCatalog& domains, const Limits& limits) {
  if (!snapshot.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "degradation snapshot generation is not established");
  }
  if (snapshot.records.size() > limits.max_degradation_records) {
    return Error(ErrorCode::LimitExceeded, "degradation population exceeds the configured maximum");
  }
  CFN_RETURN_IF_ERROR(validate(snapshot.provenance));

  std::map<std::string_view, const ResourceRecord*> catalog_index;
  for (const ResourceRecord& record : resources.resources) {
    catalog_index.emplace(record.id.view(), &record);
  }
  for (const ResourceRecord& record : resources.resources) {
    if (!record.failure_domain.valid()) {
      continue;
    }
    if (domains.find(record.failure_domain) == nullptr) {
      return Error(ErrorCode::NotFound, "resource names a failure domain that is not in the catalog",
                   record.failure_domain.view());
    }
  }

  for (const DegradationRecord& record : snapshot.records) {
    CFN_RETURN_IF_ERROR_CTX(validate(record, limits), record.resource.view());
    const auto found = catalog_index.find(record.resource.view());
    if (found == catalog_index.end()) {
      return Error(ErrorCode::NotFound, "degradation record names a resource that is not in the catalog",
                   record.resource.view());
    }
    if (found->second->generation != record.resource_generation) {
      return Error(ErrorCode::StaleResource,
                   "degradation record is bound to a resource generation that is no longer current",
                   record.resource.view());
    }
  }
  return Outcome<void>();
}

}  // namespace cfn
