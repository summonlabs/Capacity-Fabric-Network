// Capacity Fabric Network - quickstart example.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The population below is SYNTHETIC and in memory only. It demonstrates the
// library boundary: authoritative inputs go in, a generation-bound, exactly
// closing capacity answer comes out, and the explanation says where the
// capacity went.
#include <cstdio>
#include <string>

#include "cfn/cfn.hpp"

int main() {
  const cfn::Limits limits;

  auto source_id = cfn::EvidenceSourceId::parse("quickstart");
  if (!source_id.has_value()) {
    return 1;
  }
  const cfn::Timestamp observed_at = cfn::Timestamp::from_unix_seconds(1767225600LL);
  const cfn::Provenance provenance =
      cfn::observed_provenance(*source_id, cfn::ProvenanceSource::Operator, observed_at,
                               cfn::Duration{});

  cfn::FabricOptions options;
  options.limits = limits;
  auto opened = cfn::Fabric::open(options);
  if (!opened) {
    std::printf("open failed: %s\n", opened.error().to_text().c_str());
    return 1;
  }
  cfn::Fabric& fabric = **opened;

  cfn::FailureDomainCatalog domains;
  domains.generation = cfn::Generation::initial();
  domains.provenance = provenance;
  for (const char* name : {"fd-a", "fd-b"}) {
    cfn::FailureDomainRecord record;
    record.id = *cfn::FailureDomainId::parse(name);
    record.generation = cfn::Generation::initial();
    record.provenance = provenance;
    domains.domains.push_back(record);
  }

  cfn::ResourceCatalog resources;
  resources.generation = cfn::Generation::initial();
  resources.provenance = provenance;
  struct Spec {
    const char* id;
    const char* domain;
    std::uint64_t capacity;
  };
  const Spec specs[] = {
      {"a", "fd-a", 0},        {"b", "fd-b", 0},
      {"cap-ab", "fd-a", 100}, {"cap-ac", "fd-a", 40},
      {"cap-cb", "fd-b", 40},  {"cap-cd", "fd-b", 100},
      {"cap-db", "fd-a", 100},
  };
  for (const Spec& spec : specs) {
    cfn::ResourceRecord record;
    record.id = *cfn::ResourceId::parse(spec.id);
    record.generation = cfn::Generation::initial();
    record.kind = cfn::ResourceKind::Link;
    record.failure_domain = *cfn::FailureDomainId::parse(spec.domain);
    record.reported_capacity = cfn::Capacity::from_units(spec.capacity);
    record.capacity_evidence = cfn::EvidenceClass::Measured;
    record.authoritative = true;
    record.provenance = provenance;
    resources.resources.push_back(record);
  }

  cfn::Topology topology;
  topology.generation = cfn::Generation::initial();
  topology.provenance = provenance;
  for (const char* node : {"a", "b", "c", "d"}) {
    cfn::TopologyNode entry;
    entry.id = *cfn::ResourceId::parse(node);
    topology.nodes.push_back(entry);
  }
  struct EdgeSpec {
    const char* from;
    const char* to;
    const char* capacity;
  };
  const EdgeSpec edge_specs[] = {
      {"a", "b", "cap-ab"}, {"a", "c", "cap-ac"}, {"c", "b", "cap-cb"},
      {"c", "d", "cap-cd"}, {"d", "b", "cap-db"},
  };
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
    reservation.id = *cfn::ReservationId::parse("res-1");
    reservation.resource = *cfn::ResourceId::parse("cap-cd");
    reservation.resource_generation = cfn::Generation::initial();
    reservation.reservation_class = cfn::ReservationClass::Committed;
    reservation.amount = cfn::Capacity::from_units(30);
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
  policy.headroom_floor_ppm = 100000;
  policy.provenance = provenance;

  cfn::DemandShape shape;
  shape.id = *cfn::DemandShapeId::parse("shape");
  shape.generation = cfn::Generation::initial();
  shape.provenance = provenance;
  {
    cfn::FlowDemand flow;
    flow.id = *cfn::FlowId::parse("flow-1");
    flow.source = *cfn::ResourceId::parse("a");
    flow.sink = *cfn::ResourceId::parse("b");
    flow.magnitude = cfn::Capacity::from_units(50);
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

  if (!fabric.set_resource_catalog(resources) || !fabric.set_topology(topology) ||
      !fabric.set_failure_domains(domains) || !fabric.set_reservations(reservations) ||
      !fabric.set_degradation(degradation) || !fabric.register_policy(policy) ||
      !fabric.register_demand_shape(shape) || !fabric.register_model(model)) {
    std::printf("registration failed\n");
    return 1;
  }

  auto snapshot = fabric.compute(model.id);
  if (!snapshot) {
    std::printf("compute failed: %s\n", snapshot.error().to_text().c_str());
    return 1;
  }
  auto explanation = cfn::explain(*snapshot, limits);
  if (!explanation) {
    return 1;
  }
  const std::string rendered = explanation->to_text();
  std::printf("%s", rendered.c_str());
  const auto stopped = fabric.shutdown();
  return stopped ? 0 : 1;
}