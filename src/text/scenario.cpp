// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/text/scenario.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cfn/core/checked.hpp"
#include "cfn/core/text.hpp"

namespace cfn::text {

namespace {

[[nodiscard]] Error fail(std::size_t line, std::string_view message,
                        std::string_view subject = {}) {
  std::string text = "line ";
  text.append(std::to_string(line));
  text.append(": ");
  text.append(message);
  return Error(ErrorCode::MalformedInput, text, subject);
}

struct Field {
  std::string key;
  std::string value;
};

[[nodiscard]] bool split_fields(const std::vector<std::string_view>& tokens, std::size_t from,
                                std::vector<Field>& out) {
  for (std::size_t index = from; index < tokens.size(); ++index) {
    const std::string_view token = tokens[index];
    const std::size_t equals = token.find('=');
    if (equals == std::string_view::npos || equals == 0) {
      return false;
    }
    Field field;
    field.key = std::string(token.substr(0, equals));
    field.value = std::string(token.substr(equals + 1));
    out.push_back(std::move(field));
  }
  return true;
}

[[nodiscard]] const Field* find_field(const std::vector<Field>& fields, std::string_view key) {
  for (const Field& field : fields) {
    if (field.key == key) {
      return &field;
    }
  }
  return nullptr;
}

[[nodiscard]] bool parse_bool_field(const std::vector<Field>& fields, std::string_view key, bool fallback,
                                    bool& out, bool& present) {
  const Field* field = find_field(fields, key);
  if (field == nullptr) {
    out = fallback;
    present = false;
    return true;
  }
  present = true;
  return parse_bool(field->value, out);
}

[[nodiscard]] bool parse_u64_field(const std::vector<Field>& fields, std::string_view key,
                                   std::uint64_t fallback, std::uint64_t& out, bool& present) {
  const Field* field = find_field(fields, key);
  if (field == nullptr) {
    out = fallback;
    present = false;
    return true;
  }
  present = true;
  return parse_u64(field->value, out);
}

[[nodiscard]] bool parse_u32_field(const std::vector<Field>& fields, std::string_view key,
                                   std::uint32_t fallback, std::uint32_t& out, bool& present) {
  std::uint64_t value = 0;
  if (!parse_u64_field(fields, key, fallback, value, present)) {
    return false;
  }
  if (!present) {
    out = fallback;
    return true;
  }
  return checked::narrow_u32(value, out);
}

[[nodiscard]] std::uint64_t generation_value(const std::vector<Field>& fields) {
  bool present = false;
  std::uint64_t value = 1;
  (void)parse_u64_field(fields, "generation", 1, value, present);
  return value == 0 ? 1 : value;
}

[[nodiscard]] std::string bound(std::string_view value) { return std::string(value); }

}  // namespace

Outcome<Scenario> parse_scenario(std::string_view input, const Limits& limits, Timestamp now) {
  CFN_RETURN_IF_ERROR(limits.validate());
  (void)now;

  Scenario scenario;
  scenario.epoch = FabricEpoch::initial();
  scenario.resources.generation = Generation::initial();
  scenario.resources.epoch = scenario.epoch;
  scenario.topology.generation = Generation::initial();
  scenario.topology.epoch = scenario.epoch;
  scenario.domains.generation = Generation::initial();
  scenario.domains.epoch = scenario.epoch;
  scenario.reservations.generation = Generation::initial();
  scenario.reservations.epoch = scenario.epoch;
  scenario.degradation.generation = Generation::initial();
  scenario.degradation.epoch = scenario.epoch;
  scenario.policy.generation = Generation::initial();
  scenario.policy.epoch = scenario.epoch;
  scenario.demand_shape.generation = Generation::initial();
  scenario.demand_shape.epoch = scenario.epoch;
  scenario.model.generation = Generation::initial();
  scenario.model.epoch = scenario.epoch;

  const auto evidence_source = EvidenceSourceId::parse("cfn-scenario");
  const EvidenceSourceId source = evidence_source.has_value() ? *evidence_source : EvidenceSourceId{};
  const Provenance scenario_provenance = declared_provenance(source, Timestamp{}, Duration{});
  scenario.resources.provenance = scenario_provenance;
  scenario.topology.provenance = scenario_provenance;
  scenario.domains.provenance = scenario_provenance;
  scenario.reservations.provenance = scenario_provenance;
  scenario.degradation.provenance = scenario_provenance;
  scenario.policy.provenance = scenario_provenance;
  scenario.demand_shape.provenance = scenario_provenance;
  scenario.model.provenance = scenario_provenance;

  std::map<std::string, std::size_t> resource_index;
  bool version_seen = false;
  bool policy_seen = false;
  bool model_seen = false;
  std::size_t line_number = 0;

  std::size_t start = 0;
  while (start <= input.size()) {
    const std::size_t end = input.find('\n', start);
    const std::string_view raw_line =
        end == std::string_view::npos ? input.substr(start) : input.substr(start, end - start);
    start = end == std::string_view::npos ? input.size() + 1 : end + 1;
    line_number += 1;
    if (line_number > 4000000ULL) {
      return Error(ErrorCode::LimitExceeded, "scenario declares an impossible number of lines");
    }
    const std::string_view line = trim(raw_line);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const std::vector<std::string_view> tokens = split(line, ' ');
    std::vector<std::string_view> compact;
    compact.reserve(tokens.size());
    for (const std::string_view token : tokens) {
      if (!token.empty()) {
        compact.push_back(token);
      }
    }
    if (compact.empty()) {
      continue;
    }
    const std::string_view directive = compact[0];
    std::vector<std::string_view> rest(compact.begin() + 1, compact.end());

    if (!version_seen && directive != "version") {
      return fail(line_number, "the first directive must declare the format version");
    }

    if (directive == "version") {
      if (rest.size() != 1 || rest[0] != "1") {
        return fail(line_number, "only format version 1 is supported");
      }
      version_seen = true;
      continue;
    }

    if (directive == "name") {
      if (rest.size() != 1) {
        return fail(line_number, "name takes exactly one token");
      }
      scenario.name = bound(rest[0]);
      continue;
    }

    if (directive == "fabric-epoch") {
      std::uint64_t value = 0;
      if (rest.size() != 1 || !parse_u64(rest[0], value) || value == 0) {
        return fail(line_number, "fabric-epoch takes one positive integer");
      }
      scenario.epoch = FabricEpoch::from_value(value);
      scenario.resources.epoch = scenario.epoch;
      scenario.topology.epoch = scenario.epoch;
      scenario.domains.epoch = scenario.epoch;
      scenario.reservations.epoch = scenario.epoch;
      scenario.degradation.epoch = scenario.epoch;
      scenario.policy.epoch = scenario.epoch;
      scenario.demand_shape.epoch = scenario.epoch;
      scenario.model.epoch = scenario.epoch;
      continue;
    }

    if (directive == "domain") {
      if (rest.empty()) {
        return fail(line_number, "domain needs an identity");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "domain fields must be key=value pairs");
      }
      FailureDomainRecord record;
      const auto id = FailureDomainId::parse(rest[0]);
      if (!id.has_value()) {
        return fail(line_number, "domain identity is not valid", rest[0]);
      }
      record.id = *id;
      record.generation = Generation::from_value(generation_value(fields));
      bool present = false;
      if (!parse_bool_field(fields, "healthy", true, record.healthy, present)) {
        return fail(line_number, "healthy must be true or false");
      }
      record.provenance = declared_provenance(source, Timestamp{}, Duration{});
      scenario.domains.domains.push_back(record);
      continue;
    }

    if (directive == "resource") {
      if (rest.empty()) {
        return fail(line_number, "resource needs an identity");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "resource fields must be key=value pairs");
      }
      ResourceRecord record;
      const auto id = ResourceId::parse(rest[0]);
      if (!id.has_value()) {
        return fail(line_number, "resource identity is not valid", rest[0]);
      }
      record.id = *id;
      record.generation = Generation::from_value(generation_value(fields));
      bool present = false;
      std::uint64_t capacity = 0;
      if (!parse_u64_field(fields, "capacity", 0, capacity, present) || !present) {
        return fail(line_number, "resource needs a capacity=<integer> field");
      }
      record.reported_capacity = Capacity::from_units(capacity);
      std::uint64_t granularity = 0;
      if (!parse_u64_field(fields, "granularity", 0, granularity, present)) {
        return fail(line_number, "granularity must be an integer");
      }
      record.granularity = Capacity::from_units(granularity);
      EvidenceClass klass = EvidenceClass::Declared;
      if (const Field* field = find_field(fields, "evidence"); field != nullptr) {
        if (!evidence_class_from_string(field->value, klass)) {
          return fail(line_number, "evidence class is not recognised", field->value);
        }
      }
      record.capacity_evidence = klass;
      // A declared or measured capacity is authoritative unless the scenario
      // says otherwise; an evidence class that cannot carry authority never is.
      bool authoritative = may_be_authoritative(klass);
      if (!parse_bool_field(fields, "authoritative", authoritative, authoritative, present)) {
        return fail(line_number, "authoritative must be true or false");
      }
      record.authoritative = authoritative;
      bool resource_present = true;
      if (!parse_bool_field(fields, "present", true, resource_present, present)) {
        return fail(line_number, "present must be true or false");
      }
      record.present = resource_present;
      if (const Field* field = find_field(fields, "kind"); field != nullptr) {
        if (!resource_kind_from_string(field->value, record.kind)) {
          return fail(line_number, "resource kind is not recognised", field->value);
        }
      }
      if (const Field* field = find_field(fields, "fd"); field != nullptr) {
        const auto failure_domain = FailureDomainId::parse(field->value);
        if (!failure_domain.has_value()) {
          return fail(line_number, "failure domain identity is not valid", field->value);
        }
        record.failure_domain = *failure_domain;
      }
      record.provenance = authoritative
                              ? declared_provenance(source, Timestamp{}, Duration{})
                              : derived_provenance(source, Timestamp{});
      if (resource_index.count(std::string(rest[0])) != 0U) {
        return fail(line_number, "duplicate resource identity", rest[0]);
      }
      resource_index.emplace(std::string(rest[0]), scenario.resources.resources.size());
      scenario.resources.resources.push_back(record);
      continue;
    }

    if (directive == "node") {
      if (rest.empty()) {
        return fail(line_number, "node needs an identity");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "node fields must be key=value pairs");
      }
      const auto found = resource_index.find(std::string(rest[0]));
      if (found == resource_index.end()) {
        return fail(line_number, "node names a resource that was not declared earlier", rest[0]);
      }
      TopologyNode node;
      node.id = scenario.resources.resources[found->second].id;
      if (const Field* field = find_field(fields, "transit"); field != nullptr) {
        const auto transit = ResourceId::parse(field->value);
        if (!transit.has_value()) {
          return fail(line_number, "transit resource identity is not valid", field->value);
        }
        const auto transit_found = resource_index.find(field->value);
        if (transit_found == resource_index.end()) {
          return fail(line_number, "transit resource was not declared earlier", field->value);
        }
        node.transit_capacity_resource = *transit;
        std::uint64_t value = scenario.resources.resources[transit_found->second].generation.value();
        bool present = false;
        if (!parse_u64_field(fields, "transit-generation", value, value, present)) {
          return fail(line_number, "transit-generation must be an integer");
        }
        if (value == 0) {
          return fail(line_number, "transit-generation must be positive");
        }
        node.transit_resource_generation = Generation::from_value(value);
      }
      scenario.topology.nodes.push_back(node);
      continue;
    }

    if (directive == "edge") {
      if (rest.size() < 4) {
        return fail(line_number, "edge needs <edge-id> <from> <to> <capacity-resource>");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 4, fields)) {
        return fail(line_number, "edge fields must be key=value pairs");
      }
      TopologyEdge edge;
      const auto edge_id = TopologyEdgeId::parse(rest[0]);
      if (!edge_id.has_value()) {
        return fail(line_number, "edge identity is not valid", rest[0]);
      }
      const auto from = resource_index.find(std::string(rest[1]));
      if (from == resource_index.end()) {
        return fail(line_number, "edge tail was not declared earlier", rest[1]);
      }
      const auto to = resource_index.find(std::string(rest[2]));
      if (to == resource_index.end()) {
        return fail(line_number, "edge head was not declared earlier", rest[2]);
      }
      const auto capacity = resource_index.find(std::string(rest[3]));
      if (capacity == resource_index.end()) {
        return fail(line_number, "edge capacity resource was not declared earlier", rest[3]);
      }
      edge.id = *edge_id;
      edge.from = scenario.resources.resources[from->second].id;
      edge.to = scenario.resources.resources[to->second].id;
      edge.capacity_resource = scenario.resources.resources[capacity->second].id;
      std::uint64_t value = scenario.resources.resources[capacity->second].generation.value();
      bool present = false;
      if (!parse_u64_field(fields, "capacity-generation", value, value, present)) {
        return fail(line_number, "capacity-generation must be an integer");
      }
      if (value == 0) {
        return fail(line_number, "capacity-generation must be positive");
      }
      edge.capacity_resource_generation = Generation::from_value(value);
      std::uint32_t multiplicity = 1;
      if (!parse_u32_field(fields, "multiplicity", 1, multiplicity, present)) {
        return fail(line_number, "multiplicity must be a positive integer");
      }
      if (multiplicity == 0) {
        return fail(line_number, "multiplicity must be positive");
      }
      edge.multiplicity = multiplicity;
      scenario.topology.edges.push_back(edge);
      continue;
    }

    if (directive == "reservation") {
      if (rest.size() < 2) {
        return fail(line_number, "reservation needs <id> <resource>");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 2, fields)) {
        return fail(line_number, "reservation fields must be key=value pairs");
      }
      Reservation reservation;
      const auto id = ReservationId::parse(rest[0]);
      if (!id.has_value()) {
        return fail(line_number, "reservation identity is not valid", rest[0]);
      }
      const auto resource = resource_index.find(std::string(rest[1]));
      if (resource == resource_index.end()) {
        return fail(line_number, "reservation names a resource that was not declared earlier", rest[1]);
      }
      reservation.id = *id;
      reservation.resource = scenario.resources.resources[resource->second].id;
      std::uint64_t value = scenario.resources.resources[resource->second].generation.value();
      bool present = false;
      if (!parse_u64_field(fields, "resource-generation", value, value, present)) {
        return fail(line_number, "resource-generation must be an integer");
      }
      if (value == 0) {
        return fail(line_number, "resource-generation must be positive");
      }
      reservation.resource_generation = Generation::from_value(value);
      reservation.generation = Generation::from_value(generation_value(fields));
      std::uint64_t amount = 0;
      if (!parse_u64_field(fields, "amount", 0, amount, present) || !present) {
        return fail(line_number, "reservation needs an amount=<integer> field");
      }
      reservation.amount = Capacity::from_units(amount);
      std::uint32_t priority = 0;
      if (!parse_u32_field(fields, "priority", 0, priority, present)) {
        return fail(line_number, "priority must be an integer");
      }
      reservation.priority = priority;
      ReservationClass klass = ReservationClass::Committed;
      if (const Field* field = find_field(fields, "class"); field != nullptr) {
        if (!reservation_class_from_string(field->value, klass)) {
          return fail(line_number, "reservation class is not recognised", field->value);
        }
      }
      reservation.reservation_class = klass;
      reservation.provenance = declared_provenance(source, Timestamp{}, Duration{});
      scenario.reservations.reservations.push_back(reservation);
      continue;
    }

    if (directive == "degrade") {
      if (rest.empty()) {
        return fail(line_number, "degrade needs a resource");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "degrade fields must be key=value pairs");
      }
      const auto resource = resource_index.find(std::string(rest[0]));
      if (resource == resource_index.end()) {
        return fail(line_number, "degrade names a resource that was not declared earlier", rest[0]);
      }
      DegradationRecord record;
      record.resource = scenario.resources.resources[resource->second].id;
      std::uint64_t value = scenario.resources.resources[resource->second].generation.value();
      bool present = false;
      if (!parse_u64_field(fields, "resource-generation", value, value, present)) {
        return fail(line_number, "resource-generation must be an integer");
      }
      if (value == 0) {
        return fail(line_number, "resource-generation must be positive");
      }
      record.resource_generation = Generation::from_value(value);
      record.generation = Generation::from_value(generation_value(fields));
      std::uint64_t lost = 0;
      if (!parse_u64_field(fields, "lost", 0, lost, present)) {
        return fail(line_number, "lost must be an integer");
      }
      record.lost = Capacity::from_units(lost);
      std::uint32_t ppm = 0;
      if (!parse_u32_field(fields, "ppm", 0, ppm, present)) {
        return fail(line_number, "ppm must be an integer");
      }
      record.loss_ppm = ppm;
      if (const Field* field = find_field(fields, "cause"); field != nullptr) {
        DegradationCause cause = DegradationCause::Unknown;
        if (!degradation_cause_from_string(field->value, cause)) {
          return fail(line_number, "degradation cause is not recognised", field->value);
        }
        record.cause = cause;
      }
      record.provenance = observed_provenance(source, ProvenanceSource::Operator, Timestamp{},
                                              Duration{});
      scenario.degradation.records.push_back(record);
      continue;
    }

    if (directive == "policy") {
      if (rest.empty()) {
        return fail(line_number, "policy needs an identity");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "policy fields must be key=value pairs");
      }
      const auto id = PolicyId::parse(rest[0]);
      if (!id.has_value()) {
        return fail(line_number, "policy identity is not valid", rest[0]);
      }
      scenario.policy.id = *id;
      scenario.policy.generation = Generation::from_value(generation_value(fields));
      bool present = false;
      std::uint32_t ppm = 0;
      if (!parse_u32_field(fields, "headroom-ppm", 0, ppm, present)) {
        return fail(line_number, "headroom-ppm must be an integer");
      }
      scenario.policy.headroom_floor_ppm = ppm;
      std::uint64_t units = 0;
      if (!parse_u64_field(fields, "headroom-units", 0, units, present)) {
        return fail(line_number, "headroom-units must be an integer");
      }
      scenario.policy.headroom_floor_absolute = Capacity::from_units(units);
      if (const Field* field = find_field(fields, "resilience"); field != nullptr) {
        ResilienceMode mode = ResilienceMode::None;
        if (!resilience_mode_from_string(field->value, mode)) {
          return fail(line_number, "resilience mode is not recognised", field->value);
        }
        scenario.policy.resilience = mode;
      }
      if (!parse_bool_field(fields, "reject-unknown", false, scenario.policy.reject_unknown_capacity,
                            present)) {
        return fail(line_number, "reject-unknown must be true or false");
      }
      if (!parse_bool_field(fields, "require-single-slice", false,
                            scenario.policy.require_single_slice, present)) {
        return fail(line_number, "require-single-slice must be true or false");
      }
      scenario.policy.provenance = declared_provenance(source, Timestamp{}, Duration{});
      policy_seen = true;
      continue;
    }

    if (directive == "shape") {
      if (rest.empty()) {
        return fail(line_number, "shape needs an identity");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "shape fields must be key=value pairs");
      }
      const auto id = DemandShapeId::parse(rest[0]);
      if (!id.has_value()) {
        return fail(line_number, "demand shape identity is not valid", rest[0]);
      }
      scenario.demand_shape.id = *id;
      scenario.demand_shape.generation = Generation::from_value(generation_value(fields));
      bool present = false;
      std::uint64_t granularity = 0;
      if (!parse_u64_field(fields, "granularity", 0, granularity, present)) {
        return fail(line_number, "granularity must be an integer");
      }
      scenario.demand_shape.granularity = Capacity::from_units(granularity);
      scenario.demand_shape.provenance = declared_provenance(source, Timestamp{}, Duration{});
      scenario.has_demand_shape = true;
      continue;
    }

    if (directive == "flow") {
      if (rest.size() < 3) {
        return fail(line_number, "flow needs <id> <source> <sink>");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 3, fields)) {
        return fail(line_number, "flow fields must be key=value pairs");
      }
      const auto id = FlowId::parse(rest[0]);
      if (!id.has_value()) {
        return fail(line_number, "flow identity is not valid", rest[0]);
      }
      const auto source_index = resource_index.find(std::string(rest[1]));
      const auto sink_index = resource_index.find(std::string(rest[2]));
      if (source_index == resource_index.end()) {
        return fail(line_number, "flow source was not declared earlier", rest[1]);
      }
      if (sink_index == resource_index.end()) {
        return fail(line_number, "flow sink was not declared earlier", rest[2]);
      }
      FlowDemand flow;
      flow.id = *id;
      flow.source = scenario.resources.resources[source_index->second].id;
      flow.sink = scenario.resources.resources[sink_index->second].id;
      std::uint64_t magnitude = 0;
      bool present = false;
      if (!parse_u64_field(fields, "magnitude", 0, magnitude, present) || !present) {
        return fail(line_number, "flow needs a magnitude=<integer> field");
      }
      flow.magnitude = Capacity::from_units(magnitude);
      std::uint32_t priority = 0;
      if (!parse_u32_field(fields, "priority", 0, priority, present)) {
        return fail(line_number, "priority must be an integer");
      }
      flow.priority = priority;
      scenario.demand_shape.flows.push_back(flow);
      continue;
    }

    if (directive == "model") {
      if (rest.empty()) {
        return fail(line_number, "model needs an identity");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "model fields must be key=value pairs");
      }
      const auto id = CapacityModelId::parse(rest[0]);
      if (!id.has_value()) {
        return fail(line_number, "capacity model identity is not valid", rest[0]);
      }
      scenario.model.id = *id;
      scenario.model.generation = Generation::from_value(generation_value(fields));
      const Field* policy_field = find_field(fields, "policy");
      if (policy_field == nullptr) {
        return fail(line_number, "model needs a policy=<policy-id> field");
      }
      const auto policy_id = PolicyId::parse(policy_field->value);
      if (!policy_id.has_value()) {
        return fail(line_number, "model policy identity is not valid", policy_field->value);
      }
      scenario.model.policy = *policy_id;
      if (const Field* shape_field = find_field(fields, "shape"); shape_field != nullptr) {
        const auto shape_id = DemandShapeId::parse(shape_field->value);
        if (!shape_id.has_value()) {
          return fail(line_number, "model demand shape identity is not valid", shape_field->value);
        }
        scenario.model.demand_shape = *shape_id;
      }
      if (const Field* label_field = find_field(fields, "label"); label_field != nullptr) {
        if (!scenario.model.label.assign(label_field->value)) {
          return fail(line_number, "model label is too long");
        }
      }
      scenario.model.provenance = derived_provenance(source, Timestamp{});
      model_seen = true;
      continue;
    }

    if (directive == "observation") {
      if (rest.size() < 2) {
        return fail(line_number, "observation needs <resource> and fields");
      }
      std::vector<Field> fields;
      if (!split_fields(rest, 1, fields)) {
        return fail(line_number, "observation fields must be key=value pairs");
      }
      const auto resource = resource_index.find(std::string(rest[0]));
      if (resource == resource_index.end()) {
        return fail(line_number, "observation names a resource that was not declared earlier", rest[0]);
      }
      CapacityObservation observation;
      observation.resource = scenario.resources.resources[resource->second].id;
      std::uint64_t at = 0;
      bool present = false;
      if (!parse_u64_field(fields, "at", 0, at, present) || !present) {
        return fail(line_number, "observation needs an at=<unix-nanos> field");
      }
      std::int64_t signed_at = 0;
      if (!checked::fits_i64(at, signed_at)) {
        return fail(line_number, "observation timestamp is out of range");
      }
      observation.at = Timestamp::from_unix_nanos(signed_at);
      std::uint64_t usable = 0;
      if (!parse_u64_field(fields, "usable", 0, usable, present) || !present) {
        return fail(line_number, "observation needs a usable=<integer> field");
      }
      observation.observed_usable = Capacity::from_units(usable);
      observation.evidence_class = EvidenceClass::Measured;
      if (const Field* field = find_field(fields, "class"); field != nullptr) {
        EvidenceClass klass = EvidenceClass::Measured;
        if (!evidence_class_from_string(field->value, klass)) {
          return fail(line_number, "evidence class is not recognised", field->value);
        }
        observation.evidence_class = klass;
      }
      observation.provenance = observed_provenance(source, ProvenanceSource::Operator,
                                                   observation.at, Duration{});
      scenario.observations.push_back(observation);
      continue;
    }

    return fail(line_number, "unknown directive", directive);
  }

  if (!version_seen) {
    return Error(ErrorCode::MalformedInput, "scenario does not declare a format version");
  }
  if (!policy_seen) {
    const auto id = PolicyId::parse("policy");
    scenario.policy.id = id.has_value() ? *id : PolicyId{};
    scenario.policy.provenance = declared_provenance(source, Timestamp{}, Duration{});
  }
  if (!model_seen) {
    const auto id = CapacityModelId::parse("model");
    scenario.model.id = id.has_value() ? *id : CapacityModelId{};
    scenario.model.policy = scenario.policy.id;
    scenario.model.provenance = derived_provenance(source, Timestamp{});
  }
  if (scenario.has_demand_shape && !scenario.model.demand_shape.valid()) {
    scenario.model.demand_shape = scenario.demand_shape.id;
  }

  CFN_RETURN_IF_ERROR(validate(scenario.resources, limits));
  CFN_RETURN_IF_ERROR(validate(scenario.domains, limits));
  CFN_RETURN_IF_ERROR(validate(scenario.topology, scenario.resources, limits));
  CFN_RETURN_IF_ERROR(validate(scenario.reservations, scenario.resources, limits));
  CFN_RETURN_IF_ERROR(validate(scenario.degradation, scenario.resources, scenario.domains, limits));
  CFN_RETURN_IF_ERROR(validate(scenario.policy, limits));
  if (scenario.has_demand_shape) {
    CFN_RETURN_IF_ERROR(validate(scenario.demand_shape, scenario.resources, limits));
    if (scenario.demand_shape.flows.empty()) {
      return Error(ErrorCode::MalformedInput, "a demand shape must declare at least one flow");
    }
  }
  scenario.model.policy_generation = scenario.policy.generation;
  if (scenario.model.has_demand_shape()) {
    scenario.model.demand_shape_generation = scenario.demand_shape.generation;
  }
  CFN_RETURN_IF_ERROR(validate(scenario.model, limits));
  return scenario;
}

Outcome<Scenario> load_scenario(const std::filesystem::path& path, const Limits& limits, Timestamp now) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return Error(ErrorCode::NotFound, "scenario file could not be opened", path.string());
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  const std::string text = buffer.str();
  if (text.size() > limits.max_journal_bytes) {
    return Error(ErrorCode::LimitExceeded, "scenario file exceeds the configured maximum size",
                 path.string());
  }
  return parse_scenario(text, limits, now);
}

std::string format_scenario(const Scenario& scenario) {
  std::string out;
  out.append("version 1\n");
  if (!scenario.name.empty()) {
    out.append("name ").append(scenario.name).append("\n");
  }
  out.append("fabric-epoch ").append(std::to_string(scenario.epoch.value())).append("\n");
  for (const FailureDomainRecord& record : scenario.domains.domains) {
    out.append("domain ").append(record.id.view());
    out.append(" healthy=").append(record.healthy ? "true" : "false");
    out.append(" generation=").append(std::to_string(record.generation.value()));
    out.append("\n");
  }
  for (const ResourceRecord& record : scenario.resources.resources) {
    out.append("resource ").append(record.id.view());
    out.append(" kind=").append(to_string(record.kind));
    out.append(" capacity=").append(std::to_string(record.reported_capacity.units));
    if (record.failure_domain.valid()) {
      out.append(" fd=").append(record.failure_domain.view());
    }
    out.append(" evidence=").append(to_string(record.capacity_evidence));
    out.append(" authoritative=").append(record.authoritative ? "true" : "false");
    out.append(" present=").append(record.present ? "true" : "false");
    if (record.granularity.units != 0) {
      out.append(" granularity=").append(std::to_string(record.granularity.units));
    }
    out.append(" generation=").append(std::to_string(record.generation.value()));
    out.append("\n");
  }
  for (const TopologyNode& node : scenario.topology.nodes) {
    out.append("node ").append(node.id.view());
    if (node.transit_capacity_resource.valid()) {
      out.append(" transit=").append(node.transit_capacity_resource.view());
      out.append(" transit-generation=")
          .append(std::to_string(node.transit_resource_generation.value()));
    }
    out.append("\n");
  }
  for (const TopologyEdge& edge : scenario.topology.edges) {
    out.append("edge ").append(edge.id.view());
    out.append(" ").append(edge.from.view());
    out.append(" ").append(edge.to.view());
    out.append(" ").append(edge.capacity_resource.view());
    out.append(" capacity-generation=").append(std::to_string(edge.capacity_resource_generation.value()));
    if (edge.multiplicity != 1) {
      out.append(" multiplicity=").append(std::to_string(edge.multiplicity));
    }
    out.append("\n");
  }
  for (const Reservation& reservation : scenario.reservations.reservations) {
    out.append("reservation ").append(reservation.id.view());
    out.append(" ").append(reservation.resource.view());
    out.append(" resource-generation=").append(std::to_string(reservation.resource_generation.value()));
    out.append(" class=").append(to_string(reservation.reservation_class));
    out.append(" amount=").append(std::to_string(reservation.amount.units));
    out.append(" priority=").append(std::to_string(reservation.priority));
    out.append(" generation=").append(std::to_string(reservation.generation.value()));
    out.append("\n");
  }
  for (const DegradationRecord& record : scenario.degradation.records) {
    out.append("degrade ").append(record.resource.view());
    out.append(" resource-generation=").append(std::to_string(record.resource_generation.value()));
    out.append(" cause=").append(to_string(record.cause));
    if (record.lost.units != 0) {
      out.append(" lost=").append(std::to_string(record.lost.units));
    } else {
      out.append(" ppm=").append(std::to_string(record.loss_ppm));
    }
    out.append(" generation=").append(std::to_string(record.generation.value()));
    out.append("\n");
  }
  out.append("policy ").append(scenario.policy.id.view());
  out.append(" headroom-ppm=").append(std::to_string(scenario.policy.headroom_floor_ppm));
  out.append(" headroom-units=").append(std::to_string(scenario.policy.headroom_floor_absolute.units));
  out.append(" resilience=").append(to_string(scenario.policy.resilience));
  out.append(" reject-unknown=").append(scenario.policy.reject_unknown_capacity ? "true" : "false");
  out.append(" require-single-slice=").append(scenario.policy.require_single_slice ? "true" : "false");
  out.append(" generation=").append(std::to_string(scenario.policy.generation.value()));
  out.append("\n");
  if (scenario.has_demand_shape) {
    out.append("shape ").append(scenario.demand_shape.id.view());
    out.append(" granularity=").append(std::to_string(scenario.demand_shape.granularity.units));
    out.append(" generation=").append(std::to_string(scenario.demand_shape.generation.value()));
    out.append("\n");
    for (const FlowDemand& flow : scenario.demand_shape.flows) {
      out.append("flow ").append(flow.id.view());
      out.append(" ").append(flow.source.view());
      out.append(" ").append(flow.sink.view());
      out.append(" magnitude=").append(std::to_string(flow.magnitude.units));
      out.append(" priority=").append(std::to_string(flow.priority));
      out.append("\n");
    }
  }
  out.append("model ").append(scenario.model.id.view());
  out.append(" policy=").append(scenario.model.policy.view());
  if (scenario.model.has_demand_shape()) {
    out.append(" shape=").append(scenario.model.demand_shape.view());
  }
  if (!scenario.model.label.empty()) {
    out.append(" label=").append(scenario.model.label.view());
  }
  out.append(" generation=").append(std::to_string(scenario.model.generation.value()));
  out.append("\n");
  for (const CapacityObservation& observation : scenario.observations) {
    out.append("observation ").append(observation.resource.view());
    out.append(" at=").append(std::to_string(observation.at.unix_nanos));
    out.append(" usable=").append(std::to_string(observation.observed_usable.units));
    out.append(" class=").append(to_string(observation.evidence_class));
    out.append("\n");
  }
  return out;
}

}  // namespace cfn::text