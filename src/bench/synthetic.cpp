// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Every population produced here is SYNTHETIC. It is generated in memory from a
// seed and models no real network. Benchmarks and randomized tests must label
// results from it accordingly.
#include "cfn/bench/synthetic.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "cfn/core/random.hpp"

namespace cfn::bench {

namespace {

[[nodiscard]] ResourceRecord make_resource(const std::string& id, const std::string& domain,
                                           std::uint64_t capacity, ResourceKind kind) {
  ResourceRecord record;
  const auto resource = ResourceId::parse(id);
  const auto failure_domain = FailureDomainId::parse(domain);
  record.id = resource.has_value() ? *resource : ResourceId{};
  record.generation = Generation::initial();
  record.kind = kind;
  record.failure_domain = failure_domain.has_value() ? *failure_domain : FailureDomainId{};
  record.reported_capacity = Capacity::from_units(capacity);
  record.capacity_evidence = EvidenceClass::Measured;
  record.authoritative = true;
  record.present = true;
  const auto source = EvidenceSourceId::parse("synthetic-generator");
  if (source.has_value()) {
    record.provenance = observed_provenance(*source, ProvenanceSource::SyntheticGenerator,
                                            Timestamp::from_unix_seconds(1767225600LL),
                                            Duration{});
  }
  return record;
}

}  // namespace

std::string describe(const SyntheticParams& params) {
  std::string text = "SYNTHETIC resources=";
  text.append(std::to_string(params.resource_count));
  text.append(" path_width=");
  text.append(std::to_string(params.path_width));
  text.append(" reservation_ppm=");
  text.append(std::to_string(params.reservation_density_ppm));
  text.append(" failure_domains=");
  text.append(std::to_string(params.failure_domain_count));
  text.append(" flows=");
  text.append(std::to_string(params.flow_count));
  text.append(" degraded=");
  text.append(std::to_string(params.degraded_count));
  text.append(" seed=");
  text.append(std::to_string(params.seed));
  return text;
}

text::Scenario make_synthetic_scenario(const SyntheticParams& params) {
  Rng rng(params.seed, 0x5DEECE66DULL);

  text::Scenario scenario;
  scenario.name = "synthetic";
  scenario.epoch = FabricEpoch::initial();

  const std::uint32_t width = std::max<std::uint32_t>(1U, params.path_width);
  const std::uint32_t domain_count = std::max<std::uint32_t>(1U, params.failure_domain_count);
  const std::uint64_t per_resource_cost = 3ULL * static_cast<std::uint64_t>(width);
  std::uint32_t stages = 1U;
  if (params.resource_count > 2U && per_resource_cost != 0ULL) {
    const std::uint64_t computed =
        (static_cast<std::uint64_t>(params.resource_count) - 2ULL) / per_resource_cost;
    stages = static_cast<std::uint32_t>(std::max<std::uint64_t>(1ULL, computed));
  }
  stages = std::min<std::uint32_t>(stages, 512U);

  std::vector<std::string> domains;
  domains.reserve(domain_count);
  for (std::uint32_t index = 0; index < domain_count; ++index) {
    domains.push_back("fd-" + std::to_string(index));
  }
  for (const std::string& domain : domains) {
    FailureDomainRecord record;
    const auto id = FailureDomainId::parse(domain);
    record.id = id.has_value() ? *id : FailureDomainId{};
    record.generation = Generation::initial();
    record.healthy = true;
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      record.provenance = declared_provenance(*source, Timestamp::from_unix_seconds(1767225600LL),
                                              Duration{});
    }
    scenario.domains.domains.push_back(record);
  }
  scenario.domains.generation = Generation::initial();
  scenario.domains.epoch = scenario.epoch;
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.domains.provenance = declared_provenance(
          *source, Timestamp::from_unix_seconds(1767225600LL), Duration{});
    }
  }

  std::uint32_t domain_cursor = 0;
  auto next_domain = [&]() {
    const std::string& value = domains[domain_cursor % domains.size()];
    domain_cursor += 1U;
    return value;
  };

  auto node_name = [](std::uint32_t stage, std::uint32_t lane) {
    return "n-" + std::to_string(stage) + "-" + std::to_string(lane);
  };
  auto edge_name = [](std::uint32_t stage, std::uint32_t lane, std::uint32_t kind) {
    return "cap-" + std::to_string(stage) + "-" + std::to_string(lane) + "-" + std::to_string(kind);
  };

  const std::uint64_t base = params.base_capacity == 0 ? 1ULL : params.base_capacity;

  auto add_node = [&](const std::string& name, bool transit) {
    scenario.resources.resources.push_back(
        make_resource(name, next_domain(), transit ? base : 0ULL, ResourceKind::Node));
    TopologyNode node;
    const auto id = ResourceId::parse(name);
    node.id = id.has_value() ? *id : ResourceId{};
    if (transit) {
      const std::string capacity_name = name + "-transit";
      scenario.resources.resources.push_back(
          make_resource(capacity_name, next_domain(), base, ResourceKind::Bundle));
      const auto capacity_id = ResourceId::parse(capacity_name);
      node.transit_capacity_resource = capacity_id.has_value() ? *capacity_id : ResourceId{};
      node.transit_resource_generation = Generation::initial();
    }
    scenario.topology.nodes.push_back(node);
  };

  auto add_edge = [&](const std::string& from, const std::string& to, std::uint32_t stage,
                      std::uint32_t lane, std::uint32_t kind, std::uint64_t capacity) {
    const std::string capacity_name = edge_name(stage, lane, kind);
    scenario.resources.resources.push_back(
        make_resource(capacity_name, next_domain(), capacity, ResourceKind::Link));
    TopologyEdge edge;
    const auto edge_id = TopologyEdgeId::parse("e-" + capacity_name);
    const auto from_id = ResourceId::parse(from);
    const auto to_id = ResourceId::parse(to);
    const auto capacity_id = ResourceId::parse(capacity_name);
    edge.id = edge_id.has_value() ? *edge_id : TopologyEdgeId{};
    edge.from = from_id.has_value() ? *from_id : ResourceId{};
    edge.to = to_id.has_value() ? *to_id : ResourceId{};
    edge.capacity_resource = capacity_id.has_value() ? *capacity_id : ResourceId{};
    edge.capacity_resource_generation = Generation::initial();
    edge.multiplicity = 1;
    scenario.topology.edges.push_back(edge);
  };

  const std::string source_name = "src";
  const std::string sink_name = "dst";
  add_node(source_name, false);
  add_node(sink_name, false);
  for (std::uint32_t stage = 1; stage <= stages; ++stage) {
    for (std::uint32_t lane = 0; lane < width; ++lane) {
      add_node(node_name(stage, lane), true);
    }
  }

  std::uint64_t first_layer_capacity = 0;
  for (std::uint32_t lane = 0; lane < width; ++lane) {
    const std::uint64_t capacity = base;
    add_edge(source_name, node_name(1, lane), 0, lane, 0, capacity);
    first_layer_capacity += capacity;
  }
  for (std::uint32_t stage = 1; stage < stages; ++stage) {
    for (std::uint32_t lane = 0; lane < width; ++lane) {
      add_edge(node_name(stage, lane), node_name(stage + 1, lane), stage, lane, 0, base);
      if (width > 1) {
        add_edge(node_name(stage, lane), node_name(stage + 1, (lane + 1U) % width), stage, lane, 1,
                 base);
      }
    }
  }
  for (std::uint32_t lane = 0; lane < width; ++lane) {
    add_edge(node_name(stages, lane), sink_name, stages, lane, 0, base);
  }

  scenario.resources.generation = Generation::initial();
  scenario.resources.epoch = scenario.epoch;
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.resources.provenance = observed_provenance(
          *source, ProvenanceSource::SyntheticGenerator, Timestamp::from_unix_seconds(1767225600LL),
          Duration{});
    }
  }
  scenario.topology.generation = Generation::initial();
  scenario.topology.epoch = scenario.epoch;
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.topology.provenance = declared_provenance(
          *source, Timestamp::from_unix_seconds(1767225600LL), Duration{});
    }
  }

  // --- reservations ---------------------------------------------------------
  std::uint32_t reservation_ordinal = 0;
  for (const ResourceRecord& resource : scenario.resources.resources) {
    if (resource.kind != ResourceKind::Link && resource.kind != ResourceKind::Bundle) {
      continue;
    }
    if (!rng.chance(params.reservation_density_ppm)) {
      continue;
    }
    const std::uint64_t fraction_ppm = 50000ULL + (rng.bounded(150000ULL));
    const std::uint64_t amount = (resource.reported_capacity.units * fraction_ppm) / 1000000ULL;
    if (amount == 0) {
      continue;
    }
    Reservation reservation;
    const auto id = ReservationId::sequential("res", reservation_ordinal++);
    reservation.id = id;
    reservation.resource = resource.id;
    reservation.resource_generation = resource.generation;
    reservation.reservation_class = ReservationClass::Committed;
    reservation.amount = Capacity::from_units(amount);
    reservation.priority = static_cast<std::uint32_t>(rng.bounded(10ULL));
    reservation.generation = Generation::initial();
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      reservation.provenance = declared_provenance(
          *source, Timestamp::from_unix_seconds(1767225600LL), Duration{});
    }
    scenario.reservations.reservations.push_back(reservation);
  }
  scenario.reservations.generation = Generation::initial();
  scenario.reservations.epoch = scenario.epoch;
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.reservations.provenance = declared_provenance(
          *source, Timestamp::from_unix_seconds(1767225600LL), Duration{});
    }
  }

  // --- degradation ----------------------------------------------------------
  std::vector<std::uint32_t> degradable;
  for (std::uint32_t index = 0; index < scenario.resources.resources.size(); ++index) {
    if (scenario.resources.resources[index].reported_capacity.units != 0) {
      degradable.push_back(index);
    }
  }
  rng.shuffle(degradable);
  const std::uint32_t degraded_count =
      std::min<std::uint32_t>(params.degraded_count, static_cast<std::uint32_t>(degradable.size()));
  for (std::uint32_t index = 0; index < degraded_count; ++index) {
    const ResourceRecord& resource = scenario.resources.resources[degradable[index]];
    DegradationRecord record;
    record.resource = resource.id;
    record.resource_generation = resource.generation;
    record.cause = DegradationCause::Health;
    record.loss_ppm = 100000U;
    record.generation = Generation::initial();
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      record.provenance = observed_provenance(*source, ProvenanceSource::SyntheticGenerator,
                                              Timestamp::from_unix_seconds(1767225600LL), Duration{});
    }
    scenario.degradation.records.push_back(record);
  }
  scenario.degradation.generation = Generation::initial();
  scenario.degradation.epoch = scenario.epoch;
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.degradation.provenance = observed_provenance(
          *source, ProvenanceSource::SyntheticGenerator, Timestamp::from_unix_seconds(1767225600LL),
          Duration{});
    }
  }

  const std::uint32_t unhealthy =
      std::min<std::uint32_t>(params.unhealthy_domain_count,
                              static_cast<std::uint32_t>(scenario.domains.domains.size()));
  for (std::uint32_t index = 0; index < unhealthy; ++index) {
    scenario.domains.domains[index].healthy = false;
  }
  if (unhealthy != 0) {
    scenario.domains.generation = Generation::from_value(2);
  }

  // --- policy ---------------------------------------------------------------
  {
    const auto id = PolicyId::parse("synthetic-policy");
    scenario.policy.id = id.has_value() ? *id : PolicyId{};
  }
  scenario.policy.generation = Generation::initial();
  scenario.policy.epoch = scenario.epoch;
  scenario.policy.headroom_floor_ppm = params.headroom_ppm;
  scenario.policy.resilience = ResilienceMode::None;
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.policy.provenance = declared_provenance(
          *source, Timestamp::from_unix_seconds(1767225600LL), Duration{});
    }
  }

  // --- demand shape ---------------------------------------------------------
  {
    const auto id = DemandShapeId::parse("synthetic-shape");
    scenario.demand_shape.id = id.has_value() ? *id : DemandShapeId{};
  }
  scenario.demand_shape.generation = Generation::initial();
  scenario.demand_shape.epoch = scenario.epoch;
  scenario.demand_shape.granularity = Capacity{};
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.demand_shape.provenance = declared_provenance(
          *source, Timestamp::from_unix_seconds(1767225600LL), Duration{});
    }
  }
  const std::uint32_t flow_count = std::max<std::uint32_t>(1U, params.flow_count);
  const std::uint64_t magnitude = std::max<std::uint64_t>(1ULL, first_layer_capacity / 4ULL);
  for (std::uint32_t index = 0; index < flow_count; ++index) {
    FlowDemand flow;
    flow.id = FlowId::sequential("flow", index);
    const auto source = ResourceId::parse(source_name);
    const auto sink = ResourceId::parse(sink_name);
    flow.source = source.has_value() ? *source : ResourceId{};
    flow.sink = sink.has_value() ? *sink : ResourceId{};
    flow.magnitude = Capacity::from_units(magnitude);
    flow.priority = index;
    scenario.demand_shape.flows.push_back(flow);
  }
  scenario.has_demand_shape = true;

  // --- model ----------------------------------------------------------------
  {
    const auto id = CapacityModelId::parse("synthetic-model");
    scenario.model.id = id.has_value() ? *id : CapacityModelId{};
  }
  scenario.model.generation = Generation::initial();
  scenario.model.epoch = scenario.epoch;
  scenario.model.policy = scenario.policy.id;
  scenario.model.policy_generation = scenario.policy.generation;
  scenario.model.demand_shape = scenario.demand_shape.id;
  scenario.model.demand_shape_generation = scenario.demand_shape.generation;
  (void)scenario.model.label.assign("synthetic scenario");
  scenario.model.created_at = Timestamp::from_unix_seconds(1767225600LL);
  {
    const auto source = EvidenceSourceId::parse("synthetic-generator");
    if (source.has_value()) {
      scenario.model.provenance = derived_provenance(*source, Timestamp::from_unix_seconds(1767225600LL));
    }
  }

  return scenario;
}

}  // namespace cfn::bench
