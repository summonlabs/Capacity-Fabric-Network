// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/fabric/registry.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace cfn {

namespace {

/// Ensures a published generation always advances, so a consumer that binds to
/// one generation can detect every later change.
[[nodiscard]] Generation advance_generation(Generation current, Generation supplied) {
  if (current.valid()) {
    const std::optional<Generation> next = current.try_next();
    if (next.has_value() && supplied.valid() && supplied.value() >= next->value()) {
      return supplied;
    }
    return next.has_value() ? *next : current;
  }
  return supplied.valid() ? supplied : Generation::initial();
}

}  // namespace

Registry::Registry(const Limits& limits, FabricEpoch epoch, Provenance provenance)
    : state_(std::make_shared<RegistryView>()), limits_(limits), provenance_(provenance) {
  auto view = std::make_shared<RegistryView>();
  view->epoch = epoch;
  view->provenance = provenance;
  view->domains.generation = Generation::initial();
  view->domains.epoch = epoch;
  view->domains.provenance = provenance;
  view->reservations.generation = Generation::initial();
  view->reservations.epoch = epoch;
  view->reservations.provenance = provenance;
  view->degradation.generation = Generation::initial();
  view->degradation.epoch = epoch;
  view->degradation.provenance = provenance;
  view->topology.generation = Generation::initial();
  view->topology.epoch = epoch;
  view->topology.provenance = provenance;
  view->resources.generation = Generation::initial();
  view->resources.epoch = epoch;
  view->resources.provenance = provenance;
  state_ = std::move(view);
}

RegistryViewPtr Registry::view() const {
  const std::shared_lock<std::shared_mutex> guard(mutex_);
  return state_;
}

FabricEpoch Registry::epoch() const {
  const std::shared_lock<std::shared_mutex> guard(mutex_);
  return state_->epoch;
}

Outcome<void> Registry::mutate(const std::function<Outcome<void>(RegistryView&)>& operation) {
  std::unique_lock<std::shared_mutex> guard(mutex_);
  auto candidate = std::make_shared<RegistryView>(*state_);
  CFN_RETURN_IF_ERROR(operation(*candidate));
  state_ = std::move(candidate);
  return Outcome<void>();
}

Outcome<void> Registry::set_resource_catalog(ResourceCatalog catalog) {
  CFN_RETURN_IF_ERROR(validate(catalog, limits_));
  return mutate([&](RegistryView& view) -> Outcome<void> {
    catalog.generation = advance_generation(view.resources.generation, catalog.generation);
    catalog.epoch = view.epoch;
    if (!catalog.provenance.source_id.valid()) {
      catalog.provenance = provenance_;
    }
    view.resources = std::move(catalog);
    view.has_resource_catalog = true;
    return Outcome<void>();
  });
}

Outcome<void> Registry::upsert_resource(ResourceRecord record) {
  CFN_RETURN_IF_ERROR(validate(record, limits_));
  return mutate([&](RegistryView& view) -> Outcome<void> {
    ResourceCatalog& catalog = view.resources;
    catalog.generation = advance_generation(catalog.generation, Generation{});
    catalog.epoch = view.epoch;
    const auto existing = std::find_if(catalog.resources.begin(), catalog.resources.end(),
                                       [&](const ResourceRecord& candidate) {
                                         return candidate.id == record.id;
                                       });
    if (existing == catalog.resources.end()) {
      if (catalog.resources.size() >= limits_.max_resources) {
        return Error(ErrorCode::LimitExceeded, "resource population exceeds the configured maximum");
      }
      catalog.resources.push_back(record);
    } else {
      *existing = record;
    }
    std::sort(catalog.resources.begin(), catalog.resources.end(),
              [](const ResourceRecord& lhs, const ResourceRecord& rhs) { return lhs.id < rhs.id; });
    view.has_resource_catalog = true;
    return Outcome<void>();
  });
}

Outcome<void> Registry::remove_resource(const ResourceId& id) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    ResourceCatalog& catalog = view.resources;
    const auto existing = std::find_if(catalog.resources.begin(), catalog.resources.end(),
                                       [&](const ResourceRecord& candidate) {
                                         return candidate.id == id;
                                       });
    if (existing == catalog.resources.end()) {
      return Error(ErrorCode::NotFound, "resource is not registered", id.view());
    }
    catalog.resources.erase(existing);
    catalog.generation = advance_generation(catalog.generation, Generation{});
    return Outcome<void>();
  });
}

Outcome<void> Registry::set_topology(Topology topology) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    if (!view.has_resource_catalog) {
      return Error(ErrorCode::InvalidState, "a topology cannot be published before a resource catalog");
    }
    CFN_RETURN_IF_ERROR(validate(topology, view.resources, limits_));
    topology.generation = advance_generation(view.topology.generation, topology.generation);
    topology.epoch = view.epoch;
    if (!topology.provenance.source_id.valid()) {
      topology.provenance = provenance_;
    }
    view.topology = std::move(topology);
    view.has_topology = true;
    return Outcome<void>();
  });
}

Outcome<void> Registry::set_reservations(ReservationSnapshot snapshot) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    if (!view.has_resource_catalog) {
      return Error(ErrorCode::InvalidState,
                   "a reservation snapshot cannot be published before a resource catalog");
    }
    CFN_RETURN_IF_ERROR(validate(snapshot, view.resources, limits_));
    snapshot.generation = advance_generation(view.reservations.generation, snapshot.generation);
    snapshot.epoch = view.epoch;
    if (!snapshot.provenance.source_id.valid()) {
      snapshot.provenance = provenance_;
    }
    view.reservations = std::move(snapshot);
    view.has_reservations = true;
    return Outcome<void>();
  });
}

Outcome<void> Registry::set_failure_domains(FailureDomainCatalog catalog) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    CFN_RETURN_IF_ERROR(validate(catalog, limits_));
    catalog.generation = advance_generation(view.domains.generation, catalog.generation);
    catalog.epoch = view.epoch;
    if (!catalog.provenance.source_id.valid()) {
      catalog.provenance = provenance_;
    }
    view.domains = std::move(catalog);
    view.has_failure_domains = true;
    return Outcome<void>();
  });
}

Outcome<void> Registry::set_degradation(DegradationSnapshot snapshot) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    if (!view.has_resource_catalog) {
      return Error(ErrorCode::InvalidState,
                   "a degradation snapshot cannot be published before a resource catalog");
    }
    CFN_RETURN_IF_ERROR(validate(snapshot, view.resources, view.domains, limits_));
    snapshot.generation = advance_generation(view.degradation.generation, snapshot.generation);
    snapshot.epoch = view.epoch;
    if (!snapshot.provenance.source_id.valid()) {
      snapshot.provenance = provenance_;
    }
    view.degradation = std::move(snapshot);
    view.has_degradation = true;
    return Outcome<void>();
  });
}

Outcome<void> Registry::put_policy(CapacityPolicy policy) {
  CFN_RETURN_IF_ERROR(validate(policy, limits_));
  return mutate([&](RegistryView& view) -> Outcome<void> {
    const auto existing = view.policies.find(policy.id);
    if (existing == view.policies.end() && view.policies.size() >= limits_.max_policies) {
      return Error(ErrorCode::LimitExceeded, "policy population exceeds the configured maximum");
    }
    const Generation current = existing == view.policies.end() ? Generation{} : existing->second.generation;
    policy.generation = advance_generation(current, policy.generation);
    policy.epoch = view.epoch;
    if (!policy.provenance.source_id.valid()) {
      policy.provenance = provenance_;
    }
    view.policies[policy.id] = std::move(policy);
    return Outcome<void>();
  });
}

Outcome<void> Registry::remove_policy(const PolicyId& id) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    if (view.policies.erase(id) == 0U) {
      return Error(ErrorCode::NotFound, "policy is not registered", id.view());
    }
    return Outcome<void>();
  });
}

Outcome<void> Registry::put_demand_shape(DemandShape shape) {
  const RegistryViewPtr current_view = view();
  CFN_RETURN_IF_ERROR(validate(shape, current_view->resources, limits_));
  return mutate([&](RegistryView& view) -> Outcome<void> {
    const auto existing = view.demand_shapes.find(shape.id);
    if (existing == view.demand_shapes.end() &&
        view.demand_shapes.size() >= limits_.max_demand_shapes) {
      return Error(ErrorCode::LimitExceeded, "demand shape population exceeds the configured maximum");
    }
    const Generation current =
        existing == view.demand_shapes.end() ? Generation{} : existing->second.generation;
    shape.generation = advance_generation(current, shape.generation);
    shape.epoch = view.epoch;
    if (!shape.provenance.source_id.valid()) {
      shape.provenance = provenance_;
    }
    view.demand_shapes[shape.id] = std::move(shape);
    return Outcome<void>();
  });
}

Outcome<void> Registry::remove_demand_shape(const DemandShapeId& id) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    if (view.demand_shapes.erase(id) == 0U) {
      return Error(ErrorCode::NotFound, "demand shape is not registered", id.view());
    }
    return Outcome<void>();
  });
}

Outcome<void> Registry::put_model(CapacityModel model) {
  CFN_RETURN_IF_ERROR(validate(model, limits_));
  return mutate([&](RegistryView& view) -> Outcome<void> {
    const auto existing = view.models.find(model.id);
    if (existing == view.models.end() && view.models.size() >= limits_.max_models) {
      return Error(ErrorCode::LimitExceeded, "capacity model population exceeds the configured maximum");
    }
    if (view.policies.find(model.policy) == view.policies.end()) {
      return Error(ErrorCode::NotFound, "capacity model names a policy that is not registered",
                   model.policy.view());
    }
    if (model.has_demand_shape() &&
        view.demand_shapes.find(model.demand_shape) == view.demand_shapes.end()) {
      return Error(ErrorCode::NotFound, "capacity model names a demand shape that is not registered",
                   model.demand_shape.view());
    }
    const Generation current = existing == view.models.end() ? Generation{} : existing->second.generation;
    model.generation = advance_generation(current, model.generation);
    model.epoch = view.epoch;
    model.policy_generation = view.policies.at(model.policy).generation;
    if (model.has_demand_shape()) {
      model.demand_shape_generation = view.demand_shapes.at(model.demand_shape).generation;
    }
    if (!model.provenance.source_id.valid()) {
      model.provenance = provenance_;
    }
    view.models[model.id] = std::move(model);
    return Outcome<void>();
  });
}

Outcome<void> Registry::remove_model(const CapacityModelId& id) {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    if (view.models.erase(id) == 0U) {
      return Error(ErrorCode::NotFound, "capacity model is not registered", id.view());
    }
    return Outcome<void>();
  });
}

Outcome<void> Registry::record_observation(CapacityObservation observation) {
  if (!observation.resource.valid()) {
    return Error(ErrorCode::InvalidArgument, "observation has no resource");
  }
  return mutate([&](RegistryView& view) -> Outcome<void> {
    if (view.observations.size() >= limits_.max_prediction_samples) {
      view.observations.erase(view.observations.begin());
    }
    view.observations.push_back(std::move(observation));
    return Outcome<void>();
  });
}

Outcome<void> Registry::clear_observations() {
  return mutate([&](RegistryView& view) -> Outcome<void> {
    view.observations.clear();
    return Outcome<void>();
  });
}

Outcome<void> Registry::adopt(RegistryView view) {
  return mutate([&](RegistryView& target) -> Outcome<void> {
    view.epoch = target.epoch;
    target = std::move(view);
    return Outcome<void>();
  });
}

}  // namespace cfn
