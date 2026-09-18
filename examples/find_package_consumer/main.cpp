// Capacity Fabric Network - independent downstream consumer.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Uses only the installed public package. The population is SYNTHETIC and in
// memory; it demonstrates the accounting identity and the fragmentation
// result on a three node fabric.
#include <cstdio>
#include <string>

#include <cfn/cfn.hpp>

int main() {
  const cfn::Limits limits;
  const cfn::Provenance provenance = cfn::declared_provenance(
      *cfn::EvidenceSourceId::parse("consumer"), cfn::Timestamp{}, cfn::Duration{});

  cfn::FailureDomainCatalog domains;
  domains.generation = cfn::Generation::initial();
  domains.provenance = provenance;
  {
    cfn::FailureDomainRecord record;
    record.id = *cfn::FailureDomainId::parse("fd");
    record.generation = cfn::Generation::initial();
    record.provenance = provenance;
    domains.domains.push_back(record);
  }

  cfn::ResourceCatalog resources;
  resources.generation = cfn::Generation::initial();
  resources.provenance = provenance;
  struct Spec {
    const char* id;
    std::uint64_t capacity;
  };
  const Spec specs[] = {{"a", 0}, {"b", 0}, {"c", 0}, {"ab", 60}, {"bc", 60}, {"ac", 30}};
  for (const Spec& spec : specs) {
    cfn::ResourceRecord record;
    record.id = *cfn::ResourceId::parse(spec.id);
    record.generation = cfn::Generation::initial();
    record.kind = cfn::ResourceKind::Link;
    record.failure_domain = domains.domains.front().id;
    record.reported_capacity = cfn::Capacity::from_units(spec.capacity);
    record.capacity_evidence = cfn::EvidenceClass::Declared;
    record.authoritative = true;
    record.provenance = provenance;
    resources.resources.push_back(record);
  }

  cfn::Topology topology;
  topology.generation = cfn::Generation::initial();
  topology.provenance = provenance;
  for (const char* node : {"a", "b", "c"}) {
    cfn::TopologyNode entry;
    entry.id = *cfn::ResourceId::parse(node);
    topology.nodes.push_back(entry);
  }
  struct EdgeSpec {
    const char* from;
    const char* to;
    const char* capacity;
  };
  const EdgeSpec edge_specs[] = {{"a", "b", "ab"}, {"b", "c", "bc"}, {"a", "c", "ac"}};
  int ordinal = 0;
  for (const EdgeSpec& spec : edge_specs) {
    cfn::TopologyEdge edge;
    edge.id = cfn::TopologyEdgeId::sequential("edge", static_cast<std::uint64_t>(ordinal++));
    edge.from = *cfn::ResourceId::parse(spec.from);
    edge.to = *cfn::ResourceId::parse(spec.to);
    edge.capacity_resource = *cfn::ResourceId::parse(spec.capacity);
    edge.capacity_resource_generation = cfn::Generation::initial();
    topology.edges.push_back(edge);
  }

  cfn::ReservationSnapshot reservations;
  reservations.generation = cfn::Generation::initial();
  reservations.provenance = provenance;
  {
    cfn::Reservation reservation;
    reservation.id = *cfn::ReservationId::parse("res");
    reservation.resource = *cfn::ResourceId::parse("bc");
    reservation.resource_generation = cfn::Generation::initial();
    reservation.reservation_class = cfn::ReservationClass::Committed;
    reservation.amount = cfn::Capacity::from_units(10);
    reservation.generation = cfn::Generation::initial();
    reservation.provenance = provenance;
    reservations.reservations.push_back(reservation);
  }

  cfn::DegradationSnapshot degradation;
  degradation.generation = cfn::Generation::initial();
  degradation.provenance = provenance;

  cfn::CapacityPolicy policy;
  policy.id = *cfn::PolicyId::parse("policy");
  policy.generation = cfn::Generation::initial();
  policy.provenance = provenance;

  cfn::DemandShape shape;
  shape.id = *cfn::DemandShapeId::parse("shape");
  shape.generation = cfn::Generation::initial();
  shape.provenance = provenance;
  {
    cfn::FlowDemand flow;
    flow.id = *cfn::FlowId::parse("flow");
    flow.source = *cfn::ResourceId::parse("a");
    flow.sink = *cfn::ResourceId::parse("c");
    flow.magnitude = cfn::Capacity::from_units(80);
    shape.flows.push_back(flow);
  }

  cfn::CapacityModel model;
  model.id = *cfn::CapacityModelId::parse("model");
  model.generation = cfn::Generation::initial();
  model.policy = policy.id;
  model.policy_generation = policy.generation;
  model.demand_shape = shape.id;
  model.demand_shape_generation = shape.generation;
  model.provenance = provenance;

  cfn::FabricOptions options;
  options.limits = limits;
  options.provenance = provenance;
  auto fabric = cfn::Fabric::open(options);
  if (!fabric) {
    std::printf("open failed: %s\n", fabric.error().to_text().c_str());
    return 1;
  }
  cfn::Fabric& engine = **fabric;
  const bool registered =
      static_cast<bool>(engine.set_resource_catalog(resources)) &&
      static_cast<bool>(engine.set_topology(topology)) &&
      static_cast<bool>(engine.set_failure_domains(domains)) &&
      static_cast<bool>(engine.set_reservations(reservations)) &&
      static_cast<bool>(engine.set_degradation(degradation)) &&
      static_cast<bool>(engine.register_policy(policy)) &&
      static_cast<bool>(engine.register_demand_shape(shape)) &&
      static_cast<bool>(engine.register_model(model));
  if (!registered) {
    std::printf("registration failed\n");
    return 1;
  }
  auto snapshot = engine.compute(model.id);
  if (!snapshot) {
    std::printf("compute failed: %s\n", snapshot.error().to_text().c_str());
    return 1;
  }
  const cfn::AccountingRollup& rollup = snapshot->rollup;
  std::printf("cfn %s usable=%llu stranded=%llu available=%llu raw=%llu\n",
              std::string(cfn::version_string).c_str(),
              static_cast<unsigned long long>(rollup.usable_total.units),
              static_cast<unsigned long long>(rollup.stranded_total.units),
              static_cast<unsigned long long>(rollup.available_total.units),
              static_cast<unsigned long long>(rollup.raw_total.units));
  if (rollup.raw_total.units != 150ULL || rollup.available_total.units != 140ULL) {
    std::printf("unexpected accounting\n");
    return 1;
  }
  if (rollup.usable_total.units != 80ULL) {
    std::printf("unexpected usable capacity\n");
    return 1;
  }
  const auto stopped = engine.shutdown();
  return stopped ? 0 : 1;
}