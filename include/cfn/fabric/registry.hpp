// Capacity Fabric Network - authoritative in-memory registries.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The registry holds the authoritative inputs this library consumes. It uses
// copy-on-write publication: a reader obtains an immutable view with a single
// shared pointer copy and then works entirely outside the lock. No callback,
// evaluation, or persistence step ever runs while the registry lock is held,
// which removes the read-write re-entry and shutdown-deadlock failure modes.
#ifndef CFN_FABRIC_REGISTRY_HPP
#define CFN_FABRIC_REGISTRY_HPP

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/engine/prediction.hpp"
#include "cfn/model/capacity_model.hpp"
#include "cfn/model/degradation.hpp"
#include "cfn/model/demand_shape.hpp"
#include "cfn/model/policy.hpp"
#include "cfn/model/reservation.hpp"
#include "cfn/model/resource.hpp"
#include "cfn/model/topology.hpp"

namespace cfn {

/// Immutable snapshot of every authoritative input. Published by the registry
/// and consumed without holding any registry lock.
struct RegistryView {
  FabricEpoch epoch;
  Provenance provenance;
  ResourceCatalog resources;
  Topology topology;
  ReservationSnapshot reservations;
  DegradationSnapshot degradation;
  FailureDomainCatalog domains;
  std::map<PolicyId, CapacityPolicy> policies;
  std::map<DemandShapeId, DemandShape> demand_shapes;
  std::map<CapacityModelId, CapacityModel> models;
  std::vector<CapacityObservation> observations;
  bool has_resource_catalog = false;
  bool has_topology = false;
  bool has_reservations = false;
  bool has_failure_domains = false;
  bool has_degradation = false;
};

using RegistryViewPtr = std::shared_ptr<const RegistryView>;

class CFN_API Registry {
 public:
  Registry(const Limits& limits, FabricEpoch epoch, Provenance provenance);

  [[nodiscard]] RegistryViewPtr view() const;
  [[nodiscard]] FabricEpoch epoch() const;
  [[nodiscard]] const Limits& limits() const noexcept { return limits_; }

  [[nodiscard]] Outcome<void> set_resource_catalog(ResourceCatalog catalog);
  [[nodiscard]] Outcome<void> upsert_resource(ResourceRecord record);
  [[nodiscard]] Outcome<void> remove_resource(const ResourceId& id);
  [[nodiscard]] Outcome<void> set_topology(Topology topology);
  [[nodiscard]] Outcome<void> set_reservations(ReservationSnapshot snapshot);
  [[nodiscard]] Outcome<void> set_failure_domains(FailureDomainCatalog catalog);
  [[nodiscard]] Outcome<void> set_degradation(DegradationSnapshot snapshot);
  [[nodiscard]] Outcome<void> put_policy(CapacityPolicy policy);
  [[nodiscard]] Outcome<void> remove_policy(const PolicyId& id);
  [[nodiscard]] Outcome<void> put_demand_shape(DemandShape shape);
  [[nodiscard]] Outcome<void> remove_demand_shape(const DemandShapeId& id);
  [[nodiscard]] Outcome<void> put_model(CapacityModel model);
  [[nodiscard]] Outcome<void> remove_model(const CapacityModelId& id);
  [[nodiscard]] Outcome<void> record_observation(CapacityObservation observation);
  [[nodiscard]] Outcome<void> clear_observations();
  /// Replaces the whole registry contents, used by recovery.
  [[nodiscard]] Outcome<void> adopt(RegistryView view);

 private:
  /// Mutates a private copy of the current view and publishes it atomically.
  [[nodiscard]] Outcome<void> mutate(const std::function<Outcome<void>(RegistryView&)>& operation);

  mutable std::shared_mutex mutex_;
  RegistryViewPtr state_;
  Limits limits_;
  Provenance provenance_;
};

}  // namespace cfn

#endif  // CFN_FABRIC_REGISTRY_HPP