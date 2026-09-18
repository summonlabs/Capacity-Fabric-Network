// Capacity Fabric Network - shared test fixtures.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_TEST_FIXTURES_HPP
#define CFN_TEST_FIXTURES_HPP

#include <string>
#include <vector>

#include "cfn/cfn.hpp"

namespace cfn::test {

inline constexpr std::int64_t kTestEpochSeconds = 1767225600LL;

[[nodiscard]] inline Timestamp test_now() { return Timestamp::from_unix_seconds(kTestEpochSeconds); }

[[nodiscard]] inline EvidenceSourceId test_source() {
  const auto id = EvidenceSourceId::parse("test-source");
  return id.has_value() ? *id : EvidenceSourceId{};
}

[[nodiscard]] inline Provenance test_provenance() {
  return observed_provenance(test_source(), ProvenanceSource::Operator, test_now(), Duration{});
}

/// A small synthetic fabric assembled through the public model types. Every
/// population built here is generated in memory and models no physical
/// network.
struct GraphFixture {
  ResourceCatalog resources;
  Topology topology;
  ReservationSnapshot reservations;
  DegradationSnapshot degradation;
  FailureDomainCatalog domains;
  CapacityPolicy policy;
  DemandShape shape;
  CapacityModel model;
  bool has_shape = true;
  std::uint64_t generation_counter = 1;

  explicit GraphFixture(const char* policy_name = "policy", const char* model_name = "model") {
    const Provenance provenance = test_provenance();
    resources.generation = Generation::initial();
    resources.provenance = provenance;
    topology.generation = Generation::initial();
    topology.provenance = provenance;
    reservations.generation = Generation::initial();
    reservations.provenance = provenance;
    degradation.generation = Generation::initial();
    degradation.provenance = provenance;
    domains.generation = Generation::initial();
    domains.provenance = provenance;
    shape.generation = Generation::initial();
    shape.provenance = provenance;
    policy.generation = Generation::initial();
    policy.provenance = provenance;
    model.generation = Generation::initial();
    model.provenance = provenance;

    const auto policy_id = PolicyId::parse(policy_name);
    policy.id = policy_id.has_value() ? *policy_id : PolicyId{};
    const auto model_id = CapacityModelId::parse(model_name);
    model.id = model_id.has_value() ? *model_id : CapacityModelId{};
    const auto shape_id = DemandShapeId::parse("shape");
    shape.id = shape_id.has_value() ? *shape_id : DemandShapeId{};

    model.policy = policy.id;
    model.policy_generation = policy.generation;
    model.demand_shape = shape.id;
    model.demand_shape_generation = shape.generation;
    model.epoch = FabricEpoch::initial();
    model.created_at = test_now();
  }

  void add_domain(const char* id, bool healthy = true) {
    FailureDomainRecord record;
    const auto parsed = FailureDomainId::parse(id);
    record.id = parsed.has_value() ? *parsed : FailureDomainId{};
    record.generation = Generation::initial();
    record.healthy = healthy;
    record.provenance = test_provenance();
    domains.domains.push_back(record);
  }

  void add_resource(const char* id, std::uint64_t capacity, const char* failure_domain = nullptr,
                    bool authoritative = true, ResourceKind kind = ResourceKind::Link,
                    std::uint64_t granularity = 0) {
    ResourceRecord record;
    const auto parsed = ResourceId::parse(id);
    record.id = parsed.has_value() ? *parsed : ResourceId{};
    record.generation = Generation::initial();
    record.kind = kind;
    if (failure_domain != nullptr) {
      const auto domain = FailureDomainId::parse(failure_domain);
      record.failure_domain = domain.has_value() ? *domain : FailureDomainId{};
    }
    record.reported_capacity = Capacity::from_units(capacity);
    record.granularity = Capacity::from_units(granularity);
    record.capacity_evidence = authoritative ? EvidenceClass::Measured : EvidenceClass::Unknown;
    record.authoritative = authoritative;
    record.provenance = test_provenance();
    resources.resources.push_back(record);
  }

  void add_node(const char* id, const char* transit = nullptr) {
    TopologyNode node;
    const auto parsed = ResourceId::parse(id);
    node.id = parsed.has_value() ? *parsed : ResourceId{};
    if (transit != nullptr) {
      const auto capacity = ResourceId::parse(transit);
      node.transit_capacity_resource = capacity.has_value() ? *capacity : ResourceId{};
      node.transit_resource_generation = Generation::initial();
    }
    topology.nodes.push_back(node);
  }

  void add_edge(const char* from, const char* to, const char* capacity_resource) {
    TopologyEdge edge;
    edge.id = TopologyEdgeId::sequential("edge", generation_counter++);
    const auto tail = ResourceId::parse(from);
    const auto head = ResourceId::parse(to);
    const auto capacity = ResourceId::parse(capacity_resource);
    edge.from = tail.has_value() ? *tail : ResourceId{};
    edge.to = head.has_value() ? *head : ResourceId{};
    edge.capacity_resource = capacity.has_value() ? *capacity : ResourceId{};
    edge.capacity_resource_generation = Generation::initial();
    topology.edges.push_back(edge);
  }

  void add_reservation(const char* id, const char* resource, std::uint64_t amount,
                       ReservationClass klass = ReservationClass::Committed) {
    Reservation reservation;
    const auto parsed = ReservationId::parse(id);
    const auto target = ResourceId::parse(resource);
    reservation.id = parsed.has_value() ? *parsed : ReservationId{};
    reservation.resource = target.has_value() ? *target : ResourceId{};
    reservation.resource_generation = Generation::initial();
    reservation.reservation_class = klass;
    reservation.amount = Capacity::from_units(amount);
    reservation.generation = Generation::initial();
    reservation.provenance = test_provenance();
    reservations.reservations.push_back(reservation);
  }

  void add_degradation(const char* resource, std::uint32_t ppm = 0, std::uint64_t lost = 0,
                       DegradationCause cause = DegradationCause::Health) {
    DegradationRecord record;
    const auto target = ResourceId::parse(resource);
    record.resource = target.has_value() ? *target : ResourceId{};
    record.resource_generation = Generation::initial();
    record.cause = cause;
    record.lost = Capacity::from_units(lost);
    record.loss_ppm = ppm;
    record.generation = Generation::initial();
    record.provenance = test_provenance();
    degradation.records.push_back(record);
  }

  void add_flow(const char* id, const char* source, const char* sink, std::uint64_t magnitude,
                std::uint32_t priority = 0) {
    FlowDemand flow;
    const auto parsed = FlowId::parse(id);
    const auto tail = ResourceId::parse(source);
    const auto head = ResourceId::parse(sink);
    flow.id = parsed.has_value() ? *parsed : FlowId{};
    flow.source = tail.has_value() ? *tail : ResourceId{};
    flow.sink = head.has_value() ? *head : ResourceId{};
    flow.magnitude = Capacity::from_units(magnitude);
    flow.priority = priority;
    shape.flows.push_back(flow);
    has_shape = true;
  }

  [[nodiscard]] EvaluationInputs inputs() const {
    EvaluationInputs value;
    value.model = &model;
    value.policy = &policy;
    value.resources = &resources;
    value.topology = &topology;
    value.reservations = &reservations;
    value.degradation = &degradation;
    value.domains = &domains;
    value.demand_shape = has_shape ? &shape : nullptr;
    if (!has_shape) {
      value.model = &model;
    }
    return value;
  }

  [[nodiscard]] Outcome<CapacitySnapshot> evaluate(const Limits& limits = Limits()) const {
    EvaluationOptions options;
    options.limits = limits;
    options.now = test_now();
    options.epoch = FabricEpoch::initial();
    options.provenance = test_provenance();
    EvaluationInputs value = inputs();
    CapacityModel effective = model;
    if (!has_shape) {
      effective.demand_shape = DemandShapeId{};
      effective.demand_shape_generation = Generation{};
    }
    value.model = &effective;
    value.demand_shape = has_shape ? &shape : nullptr;
    return cfn::evaluate(value, options);
  }
};

/// Builds a fixture whose demand shape has no flows, so the evaluator reports
/// shape-independent accounting.
[[nodiscard]] inline GraphFixture shape_independent() {
  GraphFixture fixture;
  fixture.has_shape = false;
  fixture.model.demand_shape = DemandShapeId{};
  fixture.model.demand_shape_generation = Generation{};
  return fixture;
}

/// A diamond: src -> a (capacity ca), src -> b (capacity cb), a -> dst, b -> dst.
[[nodiscard]] inline GraphFixture diamond(std::uint64_t left, std::uint64_t right,
                                          std::uint64_t magnitude) {
  GraphFixture fixture;
  fixture.add_domain("fd-a");
  fixture.add_domain("fd-b");
  fixture.add_resource("src", 0, "fd-a", true, ResourceKind::Node);
  fixture.add_resource("dst", 0, "fd-b", true, ResourceKind::Node);
  fixture.add_resource("mid-a", 0, "fd-a", true, ResourceKind::Node);
  fixture.add_resource("mid-b", 0, "fd-b", true, ResourceKind::Node);
  fixture.add_resource("cap-sa", left, "fd-a");
  fixture.add_resource("cap-ad", left, "fd-a");
  fixture.add_resource("cap-sb", right, "fd-b");
  fixture.add_resource("cap-bd", right, "fd-b");
  fixture.add_node("src");
  fixture.add_node("dst");
  fixture.add_node("mid-a");
  fixture.add_node("mid-b");
  fixture.add_edge("src", "mid-a", "cap-sa");
  fixture.add_edge("mid-a", "dst", "cap-ad");
  fixture.add_edge("src", "mid-b", "cap-sb");
  fixture.add_edge("mid-b", "dst", "cap-bd");
  fixture.add_flow("flow-1", "src", "dst", magnitude);
  return fixture;
}

}  // namespace cfn::test

#endif  // CFN_TEST_FIXTURES_HPP