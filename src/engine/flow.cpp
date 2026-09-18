// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/engine/flow.hpp"

#include <algorithm>
#include <limits>

namespace cfn {

namespace {

/// Work is charged per arc relaxation; cancellation and the budget are checked
/// on this interval so the solver stays responsive without paying for an atomic
/// load on every edge.
constexpr std::uint64_t kCancelCheckInterval = 4096;

}  // namespace

FlowNetwork::FlowNetwork(const Limits& limits, CancellationToken cancel)
    : limits_(limits), cancel_(std::move(cancel)) {}

std::uint32_t FlowNetwork::add_node() { return node_count_++; }

std::optional<std::uint32_t> FlowNetwork::add_arc(std::uint32_t from, std::uint32_t to,
                                                  std::uint64_t capacity, std::uint32_t payload) {
  if (from >= node_count_ || to >= node_count_) {
    return std::nullopt;
  }
  const std::uint64_t arc_limit = 4ULL * (static_cast<std::uint64_t>(limits_.max_topology_edges) +
                                          static_cast<std::uint64_t>(limits_.max_topology_nodes) +
                                          static_cast<std::uint64_t>(limits_.max_resources)) +
                                  64ULL;
  if (static_cast<std::uint64_t>(arcs_.size()) + 2ULL > arc_limit) {
    return std::nullopt;
  }
  const std::uint32_t forward = static_cast<std::uint32_t>(arcs_.size());
  Arc head{};
  head.tail = from;
  head.head = to;
  head.reverse = forward + 1U;
  head.payload = payload;
  head.capacity = capacity;
  head.flow = 0;
  Arc tail{};
  tail.tail = to;
  tail.head = from;
  tail.reverse = forward;
  tail.payload = kNoPayload;
  tail.capacity = 0;
  tail.flow = 0;
  arcs_.push_back(head);
  arcs_.push_back(tail);
  return forward;
}

std::uint64_t FlowNetwork::arc_residual(std::uint32_t arc) const noexcept {
  const Arc& entry = arcs_[arc];
  const std::int64_t residual = static_cast<std::int64_t>(entry.capacity) - entry.flow;
  return residual > 0 ? static_cast<std::uint64_t>(residual) : 0ULL;
}

void FlowNetwork::set_arc_capacity(std::uint32_t arc, std::uint64_t capacity) noexcept {
  arcs_[arc].capacity = capacity;
  if (arcs_[arc].flow > static_cast<std::int64_t>(capacity)) {
    arcs_[arc].flow = static_cast<std::int64_t>(capacity);
  }
}

void FlowNetwork::reset_flows() noexcept {
  for (Arc& entry : arcs_) {
    entry.flow = 0;
  }
}

Outcome<void> FlowNetwork::build() {
  if (built_) {
    return Error(ErrorCode::InvalidState, "flow network is already built");
  }
  out_start_.assign(static_cast<std::size_t>(node_count_) + 1U, 0U);
  for (const Arc& entry : arcs_) {
    out_start_[static_cast<std::size_t>(entry.tail) + 1U] += 1U;
  }
  for (std::size_t index = 1; index < out_start_.size(); ++index) {
    out_start_[index] += out_start_[index - 1U];
  }
  out_arcs_.assign(arcs_.size(), 0U);
  std::vector<std::uint32_t> fill(out_start_.begin(), out_start_.end() - 1);
  for (std::uint32_t index = 0; index < arcs_.size(); ++index) {
    const std::uint32_t tail = arcs_[index].tail;
    out_arcs_[fill[tail]] = index;
    fill[tail] += 1U;
  }
  level_.assign(node_count_, -1);
  cursor_.assign(node_count_, 0U);
  built_ = true;
  return Outcome<void>();
}

Outcome<void> FlowNetwork::charge_work(std::uint64_t units) {
  work_units_ += units;
  if (work_units_ > limits_.max_flow_work_units) {
    return Error(ErrorCode::LimitExceeded, "maximum flow work budget exhausted");
  }
  if ((work_units_ % kCancelCheckInterval) < units && cancel_.cancelled()) {
    return Error(ErrorCode::Cancelled, "maximum flow solve was cancelled");
  }
  return Outcome<void>();
}

Outcome<std::uint64_t> FlowNetwork::max_flow(std::uint32_t source, std::uint32_t sink,
                                            std::uint64_t limit) {
  if (!built_) {
    return Error(ErrorCode::InvalidState, "flow network has not been built");
  }
  if (source >= node_count_ || sink >= node_count_) {
    return Error(ErrorCode::InvalidArgument, "flow endpoint is outside the network");
  }
  if (source == sink) {
    return 0ULL;
  }
  if (cancel_.cancelled()) {
    return Error(ErrorCode::Cancelled, "maximum flow solve was cancelled");
  }

  work_units_ = 0;
  std::uint64_t total = 0;
  bool limit_reached = false;
  std::vector<std::uint32_t> queue;
  queue.reserve(node_count_);
  std::vector<std::uint32_t> stack_nodes;
  std::vector<std::uint32_t> stack_arcs;
  stack_nodes.reserve(64);
  stack_arcs.reserve(64);

  const std::uint64_t infinity = std::numeric_limits<std::uint64_t>::max();

  for (;;) {
    // --- level graph ---
    std::fill(level_.begin(), level_.end(), -1);
    queue.clear();
    level_[source] = 0;
    queue.push_back(source);
    for (std::size_t index = 0; index < queue.size(); ++index) {
      const std::uint32_t node = queue[index];
      const std::uint32_t begin = out_start_[node];
      const std::uint32_t end = out_start_[node + 1U];
      for (std::uint32_t slot = begin; slot < end; ++slot) {
        const std::uint32_t arc = out_arcs_[slot];
        const std::uint32_t head = arcs_[arc].head;
        if (level_[head] >= 0 || arc_residual(arc) == 0) {
          continue;
        }
        level_[head] = level_[node] + 1;
        if (head == sink) {
          break;
        }
        queue.push_back(head);
      }
      CFN_RETURN_IF_ERROR(charge_work(static_cast<std::uint64_t>(end - begin) + 1ULL));
    }
    if (level_[sink] < 0) {
      break;
    }

    // --- blocking flow ---
    std::fill(cursor_.begin(), cursor_.end(), 0U);
    stack_nodes.clear();
    stack_arcs.clear();
    stack_nodes.push_back(source);
    while (!stack_nodes.empty()) {
      const std::uint32_t node = stack_nodes.back();
      if (node == sink) {
        std::uint64_t bottleneck = infinity;
        for (const std::uint32_t arc : stack_arcs) {
          bottleneck = std::min(bottleneck, arc_residual(arc));
        }
        if (limit != infinity) {
          const std::uint64_t headroom = limit > total ? limit - total : 0ULL;
          bottleneck = std::min(bottleneck, headroom);
          if (bottleneck == 0) {
            limit_reached = true;
            break;
          }
        }
        for (const std::uint32_t arc : stack_arcs) {
          arcs_[arc].flow += static_cast<std::int64_t>(bottleneck);
          arcs_[arcs_[arc].reverse].flow -= static_cast<std::int64_t>(bottleneck);
        }
        total += bottleneck;
        CFN_RETURN_IF_ERROR(charge_work(static_cast<std::uint64_t>(stack_arcs.size()) * 4ULL + 1ULL));
        std::size_t keep = 0;
        for (std::size_t index = 0; index < stack_arcs.size(); ++index) {
          if (arc_residual(stack_arcs[index]) == 0) {
            keep = index;
            break;
          }
        }
        stack_arcs.resize(keep);
        stack_nodes.resize(keep + 1U);
        continue;
      }

      bool advanced = false;
      const std::uint32_t begin = out_start_[node];
      const std::uint32_t end = out_start_[node + 1U];
      while (cursor_[node] < (end - begin)) {
        const std::uint32_t arc = out_arcs_[begin + cursor_[node]];
        ++cursor_[node];
        if (arc_residual(arc) > 0 && level_[arcs_[arc].head] == level_[node] + 1) {
          stack_arcs.push_back(arc);
          stack_nodes.push_back(arcs_[arc].head);
          advanced = true;
          break;
        }
      }
      CFN_RETURN_IF_ERROR(charge_work(static_cast<std::uint64_t>(end - begin) + 1ULL));
      if (advanced) {
        continue;
      }
      level_[node] = -1;
      stack_nodes.pop_back();
      if (!stack_arcs.empty()) {
        stack_arcs.pop_back();
      }
    }
    if (limit_reached) {
      break;
    }
  }

  // --- residual reachability for min cut attribution ---
  reachable_.assign(node_count_, 0U);
  queue.clear();
  reachable_[source] = 1U;
  queue.push_back(source);
  for (std::size_t index = 0; index < queue.size(); ++index) {
    const std::uint32_t node = queue[index];
    for (std::uint32_t slot = out_start_[node]; slot < out_start_[node + 1U]; ++slot) {
      const std::uint32_t arc = out_arcs_[slot];
      const std::uint32_t head = arcs_[arc].head;
      if (reachable_[head] == 0U && arc_residual(arc) > 0) {
        reachable_[head] = 1U;
        queue.push_back(head);
      }
    }
  }
  CFN_RETURN_IF_ERROR(charge_work(static_cast<std::uint64_t>(queue.size()) + 1ULL));
  return total;
}

}  // namespace cfn