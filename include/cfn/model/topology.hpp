// Capacity Fabric Network - authoritative topology structure.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Topology here is structure only: which capacity-bearing resources carry
// traffic between which endpoints. The library does not discover links and
// does not define link-state truth; degradation enters through the resource
// and failure-domain evidence instead.
#ifndef CFN_MODEL_TOPOLOGY_HPP
#define CFN_MODEL_TOPOLOGY_HPP

#include <cstdint>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/model/resource.hpp"

namespace cfn {

struct TopologyNode {
  ResourceId id;
  /// Resource that limits total transit through this node. A null identity
  /// means the node imposes no transit limit.
  ResourceId transit_capacity_resource;
  /// Generation the transit resource was observed at; required when a transit
  /// resource is set.
  Generation transit_resource_generation;

  [[nodiscard]] friend bool operator==(const TopologyNode&, const TopologyNode&) noexcept = default;
};

struct TopologyEdge {
  TopologyEdgeId id;
  ResourceId from;
  ResourceId to;
  /// Resource whose capacity is consumed by flow over this edge.
  ResourceId capacity_resource;
  /// Generation the capacity resource was observed at. Flow over the edge is
  /// only valid while the resource still matches this generation.
  Generation capacity_resource_generation;
  /// Number of identical parallel units aggregated into this edge. One means a
  /// single unit. The edge consumes capacity times this multiplicity.
  std::uint32_t multiplicity = 1;

  [[nodiscard]] friend bool operator==(const TopologyEdge&, const TopologyEdge&) noexcept = default;
};

struct Topology {
  Generation generation;
  FabricEpoch epoch;
  Provenance provenance;
  std::vector<TopologyNode> nodes;
  std::vector<TopologyEdge> edges;

  [[nodiscard]] const TopologyNode* find_node(const ResourceId& id) const noexcept;
};

/// Structural validation: unique identities, endpoints that exist in the
/// catalog, capacity resources that exist and match the generations the
/// topology was built against.
[[nodiscard]] CFN_API Outcome<void> validate(const Topology& topology, const ResourceCatalog& catalog,
                                             const Limits& limits);

}  // namespace cfn

#endif  // CFN_MODEL_TOPOLOGY_HPP
