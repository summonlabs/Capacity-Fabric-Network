// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/model/demand_shape.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <vector>

namespace cfn {

Outcome<void> validate(const DemandShape& shape, const ResourceCatalog& catalog, const Limits& limits) {
  if (!shape.id.valid()) {
    return Error(ErrorCode::InvalidArgument, "demand shape identity is empty");
  }
  if (!shape.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "demand shape generation is not established",
                 shape.id.view());
  }
  if (shape.flows.empty()) {
    return Error(ErrorCode::MalformedInput, "demand shape declares no flows", shape.id.view());
  }
  if (shape.flows.size() > limits.max_demand_flows) {
    return Error(ErrorCode::LimitExceeded, "demand shape flow count exceeds the configured maximum",
                 shape.id.view());
  }
  if (shape.granularity.units > limits.max_capacity_units) {
    return Error(ErrorCode::LimitExceeded, "demand shape granularity exceeds the configured maximum",
                 shape.id.view());
  }
  CFN_RETURN_IF_ERROR(validate(shape.provenance));

  std::map<std::string_view, const ResourceRecord*> catalog_index;
  for (const ResourceRecord& record : catalog.resources) {
    catalog_index.emplace(record.id.view(), &record);
  }

  std::vector<std::string_view> identities;
  identities.reserve(shape.flows.size());
  for (const FlowDemand& flow : shape.flows) {
    if (!flow.id.valid()) {
      return Error(ErrorCode::InvalidArgument, "demand flow identity is empty", shape.id.view());
    }
    if (!flow.source.valid() || !flow.sink.valid()) {
      return Error(ErrorCode::InvalidArgument, "demand flow endpoint is empty", flow.id.view());
    }
    if (flow.source == flow.sink) {
      return Error(ErrorCode::InvalidArgument, "demand flow endpoints are identical", flow.id.view());
    }
    if (flow.magnitude.units > limits.max_capacity_units) {
      return Error(ErrorCode::LimitExceeded, "demand magnitude exceeds the configured maximum",
                   flow.id.view());
    }
    if (flow.priority > limits.max_priority) {
      return Error(ErrorCode::LimitExceeded, "demand priority exceeds the configured maximum",
                   flow.id.view());
    }
    const auto source = catalog_index.find(flow.source.view());
    if (source == catalog_index.end() || !source->second->present) {
      return Error(ErrorCode::NotFound, "demand flow source is not a present resource",
                   flow.source.view());
    }
    const auto sink = catalog_index.find(flow.sink.view());
    if (sink == catalog_index.end() || !sink->second->present) {
      return Error(ErrorCode::NotFound, "demand flow sink is not a present resource", flow.sink.view());
    }
    identities.push_back(flow.id.view());
  }
  std::sort(identities.begin(), identities.end());
  for (std::size_t index = 1; index < identities.size(); ++index) {
    if (identities[index] == identities[index - 1]) {
      return Error(ErrorCode::Duplicate, "duplicate demand flow identity", identities[index]);
    }
  }
  return Outcome<void>();
}

}  // namespace cfn
