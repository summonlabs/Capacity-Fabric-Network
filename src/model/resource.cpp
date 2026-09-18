// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/model/resource.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <vector>

#include "cfn/core/hash.hpp"

namespace cfn {

std::string_view to_string(ResourceKind kind) noexcept {
  switch (kind) {
    case ResourceKind::Unknown: return "unknown";
    case ResourceKind::Port: return "port";
    case ResourceKind::Link: return "link";
    case ResourceKind::Conduit: return "conduit";
    case ResourceKind::Channel: return "channel";
    case ResourceKind::Spectrum: return "spectrum";
    case ResourceKind::Queue: return "queue";
    case ResourceKind::VirtualCircuit: return "virtual-circuit";
    case ResourceKind::Node: return "node";
    case ResourceKind::Bundle: return "bundle";
  }
  return "unknown";
}

bool resource_kind_from_string(std::string_view text, ResourceKind& out) noexcept {
  if (text == "unknown") { out = ResourceKind::Unknown; return true; }
  if (text == "port") { out = ResourceKind::Port; return true; }
  if (text == "link") { out = ResourceKind::Link; return true; }
  if (text == "conduit") { out = ResourceKind::Conduit; return true; }
  if (text == "channel") { out = ResourceKind::Channel; return true; }
  if (text == "spectrum") { out = ResourceKind::Spectrum; return true; }
  if (text == "queue") { out = ResourceKind::Queue; return true; }
  if (text == "virtual-circuit") { out = ResourceKind::VirtualCircuit; return true; }
  if (text == "node") { out = ResourceKind::Node; return true; }
  if (text == "bundle") { out = ResourceKind::Bundle; return true; }
  return false;
}

const ResourceRecord* ResourceCatalog::find(const ResourceId& id) const noexcept {
  for (const ResourceRecord& record : resources) {
    if (record.id == id) {
      return &record;
    }
  }
  return nullptr;
}

ResourceRecord* ResourceCatalog::find(const ResourceId& id) noexcept {
  for (ResourceRecord& record : resources) {
    if (record.id == id) {
      return &record;
    }
  }
  return nullptr;
}

Outcome<void> validate(const ResourceRecord& record, const Limits& limits) {
  if (!record.id.valid()) {
    return Error(ErrorCode::InvalidArgument, "resource identity is empty");
  }
  if (!record.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "resource generation is not established", record.id.view());
  }
  if (record.reported_capacity.units > limits.max_capacity_units) {
    return Error(ErrorCode::LimitExceeded, "resource capacity exceeds the configured maximum",
                 record.id.view());
  }
  if (record.granularity.units > record.reported_capacity.units) {
    return Error(ErrorCode::Contradictory, "resource granularity exceeds its reported capacity",
                 record.id.view());
  }
  if (record.authoritative && !may_be_authoritative(record.capacity_evidence)) {
    return Error(ErrorCode::NotAuthoritative,
                 "resource declares authoritative capacity with a non-observational evidence class",
                 record.id.view());
  }
  CFN_RETURN_IF_ERROR(validate(record.provenance));
  return Outcome<void>();
}

Outcome<void> validate(const ResourceCatalog& catalog, const Limits& limits) {
  if (!catalog.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "resource catalog generation is not established");
  }
  if (catalog.resources.size() > limits.max_resources) {
    return Error(ErrorCode::LimitExceeded, "resource population exceeds the configured maximum");
  }
  std::vector<std::string_view> identities;
  identities.reserve(catalog.resources.size());
  for (const ResourceRecord& record : catalog.resources) {
    CFN_RETURN_IF_ERROR_CTX(validate(record, limits), record.id.view());
    identities.push_back(record.id.view());
  }
  std::sort(identities.begin(), identities.end());
  for (std::size_t index = 1; index < identities.size(); ++index) {
    if (identities[index] == identities[index - 1]) {
      return Error(ErrorCode::Duplicate, "duplicate resource identity", identities[index]);
    }
  }
  return Outcome<void>();
}

Digest resource_set_digest(const ResourceCatalog& catalog) {
  std::vector<const ResourceRecord*> ordered;
  ordered.reserve(catalog.resources.size());
  for (const ResourceRecord& record : catalog.resources) {
    ordered.push_back(&record);
  }
  std::sort(ordered.begin(), ordered.end(), [](const ResourceRecord* lhs, const ResourceRecord* rhs) {
    return lhs->id < rhs->id;
  });
  std::uint64_t seed = hash::mix(0x9E3779B97F4A7C15ULL);
  for (const ResourceRecord* record : ordered) {
    seed = hash::combine_id(seed, record->id);
    seed = hash::combine(seed, record->generation.value());
    seed = hash::combine(seed, record->reported_capacity.units);
    seed = hash::combine(seed, record->authoritative ? 1ULL : 2ULL);
    seed = hash::combine(seed, record->present ? 3ULL : 5ULL);
  }
  Digest digest;
  digest.value = seed;
  return digest;
}

}  // namespace cfn
