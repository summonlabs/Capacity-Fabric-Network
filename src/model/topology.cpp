// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/model/topology.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <vector>

namespace cfn {

const TopologyNode* Topology::find_node(const ResourceId& id) const noexcept {
  for (const TopologyNode& node : nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

Outcome<void> validate(const Topology& topology, const ResourceCatalog& catalog, const Limits& limits) {
  if (!topology.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "topology generation is not established");
  }
  if (topology.nodes.size() > limits.max_topology_nodes) {
    return Error(ErrorCode::LimitExceeded, "topology node population exceeds the configured maximum");
  }
  if (topology.edges.size() > limits.max_topology_edges) {
    return Error(ErrorCode::LimitExceeded, "topology edge population exceeds the configured maximum");
  }
  CFN_RETURN_IF_ERROR(validate(topology.provenance));

  std::map<std::string_view, const ResourceRecord*> catalog_index;
  for (const ResourceRecord& record : catalog.resources) {
    catalog_index.emplace(record.id.view(), &record);
  }

  std::vector<std::string_view> node_ids;
  node_ids.reserve(topology.nodes.size());
  for (const TopologyNode& node : topology.nodes) {
    if (!node.id.valid()) {
      return Error(ErrorCode::InvalidArgument, "topology node identity is empty");
    }
    const auto found = catalog_index.find(node.id.view());
    if (found == catalog_index.end()) {
      return Error(ErrorCode::NotFound, "topology node is not present in the resource catalog",
                   node.id.view());
    }
    if (!found->second->present) {
      return Error(ErrorCode::Contradictory, "topology node refers to a withdrawn resource",
                   node.id.view());
    }
    if (node.transit_capacity_resource.valid()) {
      const auto transit = catalog_index.find(node.transit_capacity_resource.view());
      if (transit == catalog_index.end()) {
        return Error(ErrorCode::NotFound, "node transit capacity resource is not in the catalog",
                     node.transit_capacity_resource.view());
      }
      if (!node.transit_resource_generation.valid()) {
        return Error(ErrorCode::InvalidArgument, "node transit resource generation is not established",
                     node.id.view());
      }
      if (transit->second->generation != node.transit_resource_generation) {
        return Error(ErrorCode::StaleResource,
                     "node transit resource generation does not match the catalog",
                     node.transit_capacity_resource.view());
      }
    }
    node_ids.push_back(node.id.view());
  }
  std::sort(node_ids.begin(), node_ids.end());
  for (std::size_t index = 1; index < node_ids.size(); ++index) {
    if (node_ids[index] == node_ids[index - 1]) {
      return Error(ErrorCode::Duplicate, "duplicate topology node identity", node_ids[index]);
    }
  }

  // A capacity resource must back exactly one arc. If two edges (or an edge and
  // a node transit) drew on the same resource, the resource would become a hub
  // that lets flow leave along a different arc from the one it entered, which
  // is not a property the authoritative topology declares. The conflict is
  // reported only after the identities themselves have been checked, so a
  // duplicate identity is never masked by a sharing conflict.
  std::map<std::string_view, std::string_view> resource_owner;
  for (const TopologyNode& node : topology.nodes) {
    if (!node.transit_capacity_resource.valid()) {
      continue;
    }
    if (!resource_owner.emplace(node.transit_capacity_resource.view(), node.id.view()).second) {
      return Error(ErrorCode::Contradictory,
                   "capacity resource is claimed by more than one topology arc",
                   node.transit_capacity_resource.view());
    }
  }

  std::vector<std::string_view> edge_ids;
  edge_ids.reserve(topology.edges.size());
  for (const TopologyEdge& edge : topology.edges) {
    if (!edge.id.valid()) {
      return Error(ErrorCode::InvalidArgument, "topology edge identity is empty");
    }
    if (edge.multiplicity == 0) {
      return Error(ErrorCode::InvalidArgument, "topology edge multiplicity must be at least one",
                   edge.id.view());
    }
    if (catalog_index.find(edge.from.view()) == catalog_index.end()) {
      return Error(ErrorCode::NotFound, "topology edge tail is not present in the resource catalog",
                   edge.from.view());
    }
    if (catalog_index.find(edge.to.view()) == catalog_index.end()) {
      return Error(ErrorCode::NotFound, "topology edge head is not present in the resource catalog",
                   edge.to.view());
    }
    const auto capacity = catalog_index.find(edge.capacity_resource.view());
    if (capacity == catalog_index.end()) {
      return Error(ErrorCode::NotFound, "edge capacity resource is not present in the resource catalog",
                   edge.capacity_resource.view());
    }
    if (!edge.capacity_resource_generation.valid()) {
      return Error(ErrorCode::InvalidArgument,
                   "edge capacity resource generation is not established", edge.id.view());
    }
    if (capacity->second->generation != edge.capacity_resource_generation) {
      return Error(ErrorCode::StaleResource,
                   "edge capacity resource generation does not match the catalog",
                   edge.capacity_resource.view());
    }
    edge_ids.push_back(edge.id.view());
  }
  std::sort(edge_ids.begin(), edge_ids.end());
  for (std::size_t index = 1; index < edge_ids.size(); ++index) {
    if (edge_ids[index] == edge_ids[index - 1]) {
      return Error(ErrorCode::Duplicate, "duplicate topology edge identity", edge_ids[index]);
    }
  }

  for (const TopologyEdge& edge : topology.edges) {
    if (!resource_owner.emplace(edge.capacity_resource.view(), edge.id.view()).second) {
      return Error(ErrorCode::Contradictory,
                   "capacity resource is claimed by more than one topology arc",
                   edge.capacity_resource.view());
    }
  }
  return Outcome<void>();
}

}  // namespace cfn
