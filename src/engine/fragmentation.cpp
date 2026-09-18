// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/engine/fragmentation.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "cfn/core/cancel.hpp"
#include "cfn/core/checked.hpp"
#include "cfn/engine/flow.hpp"

namespace cfn {

namespace {

constexpr std::uint32_t kNoSlot = 0xFFFFFFFFu;

struct LogicalArc {
  std::uint32_t from = 0;
  std::uint32_t to = 0;
  std::uint32_t resource_row = kNoSlot;
  std::uint32_t solver_arc = 0;
};

struct Graph {
  Graph(const Limits& limits, CancellationToken cancel) : net(limits, std::move(cancel)) {}

  FlowNetwork net;
  std::vector<LogicalArc> arcs;
  std::vector<std::uint32_t> slot_row;
  std::vector<std::uint32_t> slot_arc;
  std::vector<std::uint32_t> slot_in;
  std::vector<std::uint32_t> slot_out;
  std::vector<std::uint32_t> row_slot;
  std::vector<std::uint32_t> flow_source_out;
  std::vector<std::uint32_t> flow_sink_in;
  std::uint32_t super_source = kNoSlot;
  std::uint32_t super_sink = kNoSlot;
  std::uint64_t infinity = 1;
  std::uint64_t available_total = 0;
  std::vector<std::uint8_t> forward_reach;
  std::vector<std::uint8_t> backward_reach;
};

[[nodiscard]] std::uint32_t node_in(std::uint32_t index) noexcept { return index * 2U; }
[[nodiscard]] std::uint32_t node_out(std::uint32_t index) noexcept { return (index * 2U) + 1U; }

[[nodiscard]] Outcome<void> add_logical(Graph& graph, std::uint32_t from, std::uint32_t to,
                                        std::uint64_t capacity, std::uint32_t resource_row) {
  const auto arc = graph.net.add_arc(from, to, capacity, resource_row);
  if (!arc.has_value()) {
    return Error(ErrorCode::LimitExceeded, "fragmentation network exceeds the configured arc budget");
  }
  LogicalArc entry;
  entry.from = from;
  entry.to = to;
  entry.resource_row = resource_row;
  entry.solver_arc = *arc;
  graph.arcs.push_back(entry);
  if (resource_row != kNoSlot) {
    graph.slot_arc.push_back(*arc);
  }
  return Outcome<void>();
}

/// Breadth first reachability over the structural graph, capacity ignored.
void reachability(const Graph& graph, std::uint32_t origin, std::vector<std::uint8_t>& out,
                  bool reverse) {
  const std::uint32_t count = graph.net.node_count();
  out.assign(count, 0U);
  if (origin >= count) {
    return;
  }
  const std::size_t arc_count = graph.arcs.size();
  std::vector<std::uint32_t> start(static_cast<std::size_t>(count) + 1U, 0U);
  for (const LogicalArc& arc : graph.arcs) {
    const std::uint32_t tail = reverse ? arc.to : arc.from;
    start[static_cast<std::size_t>(tail) + 1U] += 1U;
  }
  for (std::size_t index = 1; index < start.size(); ++index) {
    start[index] += start[index - 1U];
  }
  std::vector<std::uint32_t> heads(arc_count, 0U);
  std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
  for (std::size_t index = 0; index < arc_count; ++index) {
    const std::uint32_t tail = reverse ? graph.arcs[index].to : graph.arcs[index].from;
    heads[fill[tail]] = reverse ? graph.arcs[index].from : graph.arcs[index].to;
    fill[tail] += 1U;
  }
  std::vector<std::uint32_t> queue;
  queue.reserve(count);
  out[origin] = 1U;
  queue.push_back(origin);
  for (std::size_t index = 0; index < queue.size(); ++index) {
    const std::uint32_t node = queue[index];
    for (std::uint32_t slot = start[node]; slot < start[static_cast<std::size_t>(node) + 1U]; ++slot) {
      const std::uint32_t head = heads[slot];
      if (out[head] == 0U) {
        out[head] = 1U;
        queue.push_back(head);
      }
    }
  }
}

[[nodiscard]] Outcome<std::unique_ptr<Graph>> assemble(const std::vector<ResourceAccounting>& rows,
                                                       const Topology& topology,
                                                       const std::vector<const FlowDemand*>& flows,
                                                       const Limits& limits,
                                                       const CancellationToken& cancel) {
  auto graph = std::make_unique<Graph>(limits, cancel);

  std::map<std::string_view, std::uint32_t> row_index;
  for (std::uint32_t index = 0; index < rows.size(); ++index) {
    row_index.emplace(rows[index].resource.view(), index);
  }
  std::map<std::string_view, std::uint32_t> node_index;
  for (std::uint32_t index = 0; index < topology.nodes.size(); ++index) {
    if (row_index.find(topology.nodes[index].id.view()) == row_index.end()) {
      return Outcome<std::unique_ptr<Graph>>(
          Error(ErrorCode::NotFound, "topology node is not part of the accounting population",
                topology.nodes[index].id.view()));
    }
    node_index.emplace(topology.nodes[index].id.view(), index);
  }

  graph->row_slot.assign(rows.size(), kNoSlot);
  std::map<std::string_view, std::uint32_t> slot_index;

  auto slot_for = [&](const ResourceId& id) -> Outcome<std::uint32_t> {
    const auto existing = slot_index.find(id.view());
    if (existing != slot_index.end()) {
      return existing->second;
    }
    const auto row = row_index.find(id.view());
    if (row == row_index.end()) {
      return Error(ErrorCode::NotFound, "capacity resource is not part of the accounting population",
                   id.view());
    }
    const std::uint32_t slot = static_cast<std::uint32_t>(graph->slot_row.size());
    graph->slot_row.push_back(row->second);
    graph->row_slot[row->second] = slot;
    slot_index.emplace(id.view(), slot);
    return slot;
  };

  for (const TopologyNode& node : topology.nodes) {
    if (node.transit_capacity_resource.valid()) {
      const Outcome<std::uint32_t> slot = slot_for(node.transit_capacity_resource);
      if (!slot) {
        return Outcome<std::unique_ptr<Graph>>(slot.error());
      }
    }
  }
  for (const TopologyEdge& edge : topology.edges) {
    const Outcome<std::uint32_t> slot = slot_for(edge.capacity_resource);
    if (!slot) {
      return Outcome<std::unique_ptr<Graph>>(slot.error());
    }
  }

  checked::Accumulator total;
  for (const ResourceAccounting& row : rows) {
    (void)total.add(row.available.units);
  }
  if (!total.valid()) {
    return Outcome<std::unique_ptr<Graph>>(
        Error(ErrorCode::Overflow, "available capacity total overflows the supported range"));
  }
  graph->available_total = total.value();

  const std::uint32_t node_count = static_cast<std::uint32_t>(topology.nodes.size());
  const std::uint32_t slot_count = static_cast<std::uint32_t>(graph->slot_row.size());
  for (std::uint32_t index = 0; index < node_count; ++index) {
    (void)graph->net.add_node();
    (void)graph->net.add_node();
  }
  graph->slot_in.reserve(slot_count);
  graph->slot_out.reserve(slot_count);
  for (std::uint32_t index = 0; index < slot_count; ++index) {
    graph->slot_in.push_back(graph->net.add_node());
    graph->slot_out.push_back(graph->net.add_node());
  }
  graph->super_source = graph->net.add_node();
  graph->super_sink = graph->net.add_node();
  graph->infinity = graph->available_total == 0 ? 1ULL : graph->available_total;

  for (std::uint32_t slot = 0; slot < slot_count; ++slot) {
    CFN_RETURN_IF_ERROR(add_logical(*graph, graph->slot_in[slot], graph->slot_out[slot],
                                    rows[graph->slot_row[slot]].available.units,
                                    graph->slot_row[slot]));
  }

  for (std::uint32_t index = 0; index < node_count; ++index) {
    const TopologyNode& node = topology.nodes[index];
    if (!node.transit_capacity_resource.valid()) {
      CFN_RETURN_IF_ERROR(add_logical(*graph, node_in(index), node_out(index), graph->infinity, kNoSlot));
      continue;
    }
    const auto slot = slot_index.find(node.transit_capacity_resource.view());
    if (slot == slot_index.end()) {
      return Outcome<std::unique_ptr<Graph>>(
          Error(ErrorCode::NotFound, "node transit capacity resource is not available",
                node.transit_capacity_resource.view()));
    }
    CFN_RETURN_IF_ERROR(
        add_logical(*graph, node_in(index), graph->slot_in[slot->second], graph->infinity, kNoSlot));
    CFN_RETURN_IF_ERROR(
        add_logical(*graph, graph->slot_out[slot->second], node_out(index), graph->infinity, kNoSlot));
  }

  for (const TopologyEdge& edge : topology.edges) {
    const auto from_node = node_index.find(edge.from.view());
    const auto to_node = node_index.find(edge.to.view());
    if (from_node == node_index.end() || to_node == node_index.end()) {
      return Outcome<std::unique_ptr<Graph>>(
          Error(ErrorCode::NotFound, "topology edge endpoint is not a topology node", edge.id.view()));
    }
    const auto slot = slot_index.find(edge.capacity_resource.view());
    if (slot == slot_index.end()) {
      return Outcome<std::unique_ptr<Graph>>(
          Error(ErrorCode::NotFound, "edge capacity resource is not available",
                edge.capacity_resource.view()));
    }
    CFN_RETURN_IF_ERROR(add_logical(*graph, node_out(from_node->second),
                                    graph->slot_in[slot->second], graph->infinity, kNoSlot));
    CFN_RETURN_IF_ERROR(add_logical(*graph, graph->slot_out[slot->second],
                                    node_in(to_node->second), graph->infinity, kNoSlot));
  }

  graph->flow_source_out.reserve(flows.size());
  graph->flow_sink_in.reserve(flows.size());
  for (const FlowDemand* flow : flows) {
    const auto source = node_index.find(flow->source.view());
    if (source == node_index.end()) {
      return Outcome<std::unique_ptr<Graph>>(
          Error(ErrorCode::NotFound, "demand flow source is not a topology node", flow->source.view()));
    }
    const auto sink = node_index.find(flow->sink.view());
    if (sink == node_index.end()) {
      return Outcome<std::unique_ptr<Graph>>(
          Error(ErrorCode::NotFound, "demand flow sink is not a topology node", flow->sink.view()));
    }
    graph->flow_source_out.push_back(node_out(source->second));
    graph->flow_sink_in.push_back(node_in(sink->second));
    CFN_RETURN_IF_ERROR(
        add_logical(*graph, graph->super_source, node_out(source->second), graph->infinity, kNoSlot));
    CFN_RETURN_IF_ERROR(
        add_logical(*graph, node_in(sink->second), graph->super_sink, graph->infinity, kNoSlot));
  }

  CFN_RETURN_IF_ERROR(graph->net.build());
  reachability(*graph, graph->super_source, graph->forward_reach, false);
  reachability(*graph, graph->super_sink, graph->backward_reach, true);
  return Outcome<std::unique_ptr<Graph>>(std::move(graph));
}

[[nodiscard]] bool row_admissible(const Graph& graph, std::uint32_t row) {
  const std::uint32_t slot = graph.row_slot[row];
  if (slot == kNoSlot) {
    return false;
  }
  return graph.forward_reach[graph.slot_in[slot]] != 0U &&
         graph.backward_reach[graph.slot_out[slot]] != 0U;
}

[[nodiscard]] std::uint64_t quantize_down(std::uint64_t value, std::uint64_t granularity) noexcept {
  if (granularity == 0) {
    return value;
  }
  return (value / granularity) * granularity;
}

/// Failure domain loss scenarios for the requested resilience mode.
[[nodiscard]] Outcome<std::vector<std::vector<std::uint32_t>>> build_scenarios(
    const std::vector<ResourceAccounting>& rows, const std::vector<std::uint32_t>& active_rows,
    ResilienceMode mode, const Limits& limits) {
  std::vector<std::vector<std::uint32_t>> scenarios;
  if (mode == ResilienceMode::None) {
    return scenarios;
  }
  std::map<FailureDomainId, std::vector<std::uint32_t>> by_domain;
  for (const std::uint32_t row : active_rows) {
    if (!rows[row].failure_domain.valid()) {
      continue;
    }
    if (!rows[row].domain_healthy) {
      // An already lost domain needs no scenario: its capacity is already gone.
      continue;
    }
    by_domain[rows[row].failure_domain].push_back(row);
  }
  if (by_domain.empty()) {
    return scenarios;
  }
  std::vector<std::vector<std::uint32_t>> groups;
  groups.reserve(by_domain.size());
  for (auto& entry : by_domain) {
    groups.push_back(std::move(entry.second));
  }
  if (mode == ResilienceMode::DomainN1) {
    if (groups.size() > limits.max_resilience_domains_scanned) {
      return Error(ErrorCode::LimitExceeded,
                   "resilience scenario budget is too small for the failure domain population");
    }
    for (auto& group : groups) {
      scenarios.push_back(group);
    }
    return scenarios;
  }
  const std::uint64_t pair_count =
      (static_cast<std::uint64_t>(groups.size()) * (static_cast<std::uint64_t>(groups.size()) - 1ULL)) / 2ULL;
  if (pair_count > limits.max_resilience_domains_scanned) {
    return Error(ErrorCode::LimitExceeded,
                 "resilience scenario budget is too small for the N-1-1 failure domain combinations");
  }
  for (std::size_t first = 0; first < groups.size(); ++first) {
    for (std::size_t second = first + 1; second < groups.size(); ++second) {
      std::vector<std::uint32_t> combined = groups[first];
      combined.insert(combined.end(), groups[second].begin(), groups[second].end());
      scenarios.push_back(std::move(combined));
    }
  }
  return scenarios;
}

}  // namespace

Outcome<FragmentationResult> analyse_fragmentation(std::vector<ResourceAccounting>& rows,
                                                   const Topology& topology,
                                                   const CapacityPolicy& policy,
                                                   const DemandShape* shape, const Limits& limits,
                                                   const CancellationToken& cancel) {
  FragmentationResult result;
  if (shape == nullptr) {
    return result;
  }
  if (cancel.cancelled()) {
    return Error(ErrorCode::Cancelled, "fragmentation analysis was cancelled before it started");
  }

  std::vector<std::uint32_t> order(shape->flows.size(), 0U);
  for (std::uint32_t index = 0; index < order.size(); ++index) {
    order[index] = index;
  }
  std::sort(order.begin(), order.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
    const FlowDemand& left = shape->flows[lhs];
    const FlowDemand& right = shape->flows[rhs];
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    }
    return left.id < right.id;
  });
  std::vector<const FlowDemand*> flows;
  flows.reserve(order.size());
  for (const std::uint32_t index : order) {
    flows.push_back(&shape->flows[index]);
  }

  auto assembled = assemble(rows, topology, flows, limits, cancel);
  if (!assembled) {
    return assembled.error();
  }
  Graph& graph = **assembled;

  result.evaluated = true;
  result.exact = flows.size() == 1;

  const Outcome<std::uint64_t> base = graph.net.max_flow(graph.super_source, graph.super_sink);
  if (!base) {
    return base.error();
  }
  result.deliverable_unresilient = Capacity::from_units(*base);

  std::vector<std::uint64_t> used(rows.size(), 0ULL);
  for (std::uint32_t row = 0; row < rows.size(); ++row) {
    const std::uint32_t slot = graph.row_slot[row];
    if (slot != kNoSlot) {
      used[row] = graph.net.arc_flow(graph.slot_arc[slot]);
    }
  }

  std::vector<std::uint32_t> active_rows;
  active_rows.reserve(rows.size());
  for (std::uint32_t row = 0; row < rows.size(); ++row) {
    // A resource that no topology arc draws on lies on no admissible path, so
    // its capacity is stranded by segmentation for any demand shape.
    rows[row].admissible = false;
    if (graph.row_slot[row] != kNoSlot) {
      rows[row].admissible = row_admissible(graph, row);
      active_rows.push_back(row);
    }
  }

  std::uint64_t deliverable = *base;
  if (policy.resilience != ResilienceMode::None) {
    const Outcome<std::vector<std::vector<std::uint32_t>>> scenarios =
        build_scenarios(rows, active_rows, policy.resilience, limits);
    if (!scenarios) {
      return scenarios.error();
    }
    for (const std::vector<std::uint32_t>& scenario : *scenarios) {
      if (cancel.cancelled()) {
        return Error(ErrorCode::Cancelled, "fragmentation analysis was cancelled");
      }
      for (const std::uint32_t row : scenario) {
        const std::uint32_t slot = graph.row_slot[row];
        if (slot != kNoSlot) {
          graph.net.set_arc_capacity(graph.slot_arc[slot], 0ULL);
        }
      }
      graph.net.reset_flows();
      const Outcome<std::uint64_t> value = graph.net.max_flow(graph.super_source, graph.super_sink);
      if (!value) {
        return value.error();
      }
      deliverable = std::min(deliverable, *value);
      for (const std::uint32_t row : scenario) {
        const std::uint32_t slot = graph.row_slot[row];
        if (slot != kNoSlot) {
          graph.net.set_arc_capacity(graph.slot_arc[slot], rows[row].available.units);
        }
      }
      result.resilience_scenarios += 1U;
    }
    result.resilience_applied = result.resilience_scenarios != 0U;
  }
  result.deliverable = Capacity::from_units(deliverable);

  // --- exact attribution ----------------------------------------------------
  std::uint64_t available_total = 0;
  {
    checked::Accumulator accumulator;
    for (const ResourceAccounting& row : rows) {
      (void)accumulator.add(row.available.units);
    }
    if (!accumulator.valid()) {
      return Error(ErrorCode::Overflow, "available capacity total overflows the supported range");
    }
    available_total = accumulator.value();
  }
  if (deliverable > available_total) {
    return Error(ErrorCode::Contradictory,
                 "fit produced more deliverable capacity than the fabric has available");
  }
  const std::uint64_t stranded_total = available_total - deliverable;
  const std::uint64_t resilience_stranded = *base - deliverable;

  std::uint64_t segmentation = 0;
  {
    checked::Accumulator accumulator;
    for (const ResourceAccounting& row : rows) {
      if (!row.admissible) {
        (void)accumulator.add(row.available.units);
      }
    }
    if (!accumulator.valid()) {
      return Error(ErrorCode::Overflow, "segmentation total overflows the supported range");
    }
    segmentation = accumulator.value();
  }
  if (segmentation > stranded_total) {
    return Error(ErrorCode::Contradictory,
                 "segmentation stranding exceeds the total stranded capacity");
  }
  if (resilience_stranded > stranded_total - segmentation) {
    return Error(ErrorCode::Contradictory,
                 "resilience stranding exceeds the remaining stranded capacity");
  }
  const std::uint64_t bottleneck = stranded_total - segmentation - resilience_stranded;

  result.stranding.segmentation = Capacity::from_units(segmentation);
  result.stranding.bottleneck = Capacity::from_units(bottleneck);
  result.stranding.failure_domain_resilience = Capacity::from_units(resilience_stranded);

  std::uint64_t reserved_stranded = 0;
  {
    checked::Accumulator accumulator;
    for (const ResourceAccounting& row : rows) {
      if (!row.admissible) {
        (void)accumulator.add(row.reserved.units);
      }
    }
    if (accumulator.valid()) {
      reserved_stranded = accumulator.value();
    }
  }
  result.reserved_stranded = Capacity::from_units(reserved_stranded);

  // Per-resource witnesses. A unit of flow can occupy several resources, so the
  // per-resource figure is an upper bound witness; the exact totals are the
  // breakdown above.
  for (std::uint32_t row = 0; row < rows.size(); ++row) {
    ResourceAccounting& entry = rows[row];
    entry.stranded = Capacity{};
    entry.strand_cause = StrandCause::None;
    if (!entry.admissible) {
      entry.stranded = entry.available;
      entry.strand_cause = StrandCause::Segmentation;
      continue;
    }
    const std::uint32_t slot = graph.row_slot[row];
    if (slot == kNoSlot) {
      continue;
    }
    const std::uint64_t residual =
        entry.available.units > used[row] ? entry.available.units - used[row] : 0ULL;
    if (residual != 0) {
      entry.stranded = Capacity::from_units(residual);
      entry.strand_cause = StrandCause::Bottleneck;
    }
  }

  std::vector<std::pair<std::uint64_t, const ResourceAccounting*>> witnesses;
  for (const ResourceAccounting& row : rows) {
    if (row.admissible && row.stranded.units != 0) {
      witnesses.emplace_back(row.stranded.units, &row);
    }
  }
  std::sort(witnesses.begin(), witnesses.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.first != rhs.first) {
      return lhs.first > rhs.first;
    }
    return lhs.second->resource < rhs.second->resource;
  });
  for (const auto& witness : witnesses) {
    if (result.bottleneck_resources.size() >= limits.max_bottleneck_resources) {
      break;
    }
    result.bottleneck_resources.push_back(witness.second->resource);
  }

  std::vector<std::pair<std::uint64_t, const ResourceAccounting*>> isolated;
  for (const ResourceAccounting& row : rows) {
    if (!row.admissible && row.available.units != 0) {
      isolated.emplace_back(row.available.units, &row);
    }
  }
  std::sort(isolated.begin(), isolated.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.first != rhs.first) {
      return lhs.first > rhs.first;
    }
    return lhs.second->resource < rhs.second->resource;
  });
  for (const auto& entry : isolated) {
    if (result.segmentation_resources.size() >= limits.max_bottleneck_resources) {
      break;
    }
    result.segmentation_resources.push_back(entry.second->resource);
  }

  // --- deterministic feasible allocation ------------------------------------
  graph.net.reset_flows();
  const std::uint64_t granularity = shape->granularity.units;
  std::uint64_t budget = deliverable;
  std::uint64_t satisfied = 0;
  std::uint64_t granularity_loss = 0;
  std::uint64_t quantized_demand = 0;

  result.flows.reserve(flows.size());
  for (std::size_t index = 0; index < flows.size(); ++index) {
    const FlowDemand& flow = *flows[index];
    FlowFit fit;
    fit.id = flow.id;
    fit.source = flow.source;
    fit.sink = flow.sink;
    fit.requested = flow.magnitude;

    const std::uint64_t cap = quantize_down(flow.magnitude.units, granularity);
    std::uint64_t next_quantized = 0;
    if (!checked::add_u64(quantized_demand, cap, next_quantized)) {
      return Error(ErrorCode::Overflow, "quantized demand total overflows");
    }
    quantized_demand = next_quantized;

    const std::uint64_t limit = std::min(cap, budget);
    const Outcome<std::uint64_t> pushed =
        graph.net.max_flow(graph.flow_source_out[index], graph.flow_sink_in[index], limit);
    if (!pushed) {
      return pushed.error();
    }
    std::uint64_t admitted = quantize_down(*pushed, granularity);
    if (policy.require_single_slice && admitted < flow.magnitude.units) {
      admitted = 0;
    }
    granularity_loss += *pushed - admitted;
    budget -= *pushed;
    {
      std::uint64_t next = 0;
      if (!checked::add_u64(satisfied, admitted, next)) {
        return Error(ErrorCode::Overflow, "satisfied capacity total overflows");
      }
      satisfied = next;
    }
    fit.admitted = Capacity::from_units(admitted);
    fit.unmet = Capacity::from_units(flow.magnitude.units - admitted);
    fit.satisfied = admitted >= flow.magnitude.units;
    result.flows.push_back(fit);
  }

  if (satisfied > deliverable) {
    return Error(ErrorCode::Contradictory, "placed demand exceeds the deliverable capacity envelope");
  }
  result.satisfied = Capacity::from_units(satisfied);
  result.spare = Capacity::from_units(deliverable - satisfied);
  result.granularity_loss = Capacity::from_units(granularity_loss);
  result.satisfied_upper =
      Capacity::from_units(std::min(quantized_demand, static_cast<std::uint64_t>(deliverable)));
  return result;
}

Outcome<FitQueryResult> fit_query(std::vector<ResourceAccounting>& rows, const Topology& topology,
                                  const FitQuery& query, const Limits& limits,
                                  const CancellationToken& cancel) {
  if (!query.source.valid() || !query.sink.valid()) {
    return Error(ErrorCode::InvalidArgument, "fit query needs both endpoints");
  }
  if (query.source == query.sink) {
    return Error(ErrorCode::InvalidArgument, "fit query endpoints are identical", query.source.view());
  }
  if (query.magnitude.units > limits.max_capacity_units) {
    return Error(ErrorCode::LimitExceeded, "fit query magnitude exceeds the configured maximum");
  }

  const auto shape_id = DemandShapeId::parse("fit-query");
  const auto flow_id = FlowId::parse("fit-flow");
  if (!shape_id.has_value() || !flow_id.has_value()) {
    return Error(ErrorCode::InvalidState, "fit query identity templates are invalid");
  }
  const auto source_id = EvidenceSourceId::parse("fit-query");
  if (!source_id.has_value()) {
    return Error(ErrorCode::InvalidState, "fit query source identity template is invalid");
  }

  DemandShape shape;
  shape.id = *shape_id;
  shape.generation = Generation::initial();
  shape.provenance = derived_provenance(*source_id, Timestamp{});
  shape.granularity = query.granularity;
  FlowDemand flow;
  flow.id = *flow_id;
  flow.source = query.source;
  flow.sink = query.sink;
  flow.magnitude = query.magnitude;
  flow.priority = 0;
  shape.flows.push_back(flow);

  CapacityPolicy policy;
  const auto policy_id = PolicyId::parse("fit-policy");
  if (!policy_id.has_value()) {
    return Error(ErrorCode::InvalidState, "fit query policy identity template is invalid");
  }
  policy.id = *policy_id;
  policy.generation = Generation::initial();
  policy.resilience = query.resilience;
  policy.provenance = derived_provenance(*source_id, Timestamp{});

  Outcome<FragmentationResult> analysis =
      analyse_fragmentation(rows, topology, policy, &shape, limits, cancel);
  if (!analysis) {
    return analysis.error();
  }
  const FragmentationResult& result = *analysis;

  FitQueryResult answer;
  answer.exact = true;
  answer.admitted = result.flows.empty() ? Capacity{} : result.flows.front().admitted;
  answer.deliverable = result.deliverable;
  answer.bottleneck_capacity = result.deliverable;
  answer.stranding = result.stranding;
  answer.resilience_scenarios = result.resilience_scenarios;
  answer.bottleneck_resources = result.bottleneck_resources;
  answer.segmentation_resources = result.segmentation_resources;
  const std::uint64_t quantized = quantize_down(query.magnitude.units, query.granularity.units);
  answer.satisfiable = answer.admitted.units >= quantized;
  return answer;
}

}  // namespace cfn