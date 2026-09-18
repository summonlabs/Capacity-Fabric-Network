// Capacity Fabric Network - bounded maximum-flow solver.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Fragmentation fit queries reduce to maximum flow with node and resource
// capacities. The solver is Dinic with an explicit work budget: a pathological
// or adversarial graph is rejected with LimitExceeded rather than running for
// an unbounded time. Traversal order is derived from arc insertion order, which
// is itself derived from sorted identities, so results are deterministic.
#ifndef CFN_ENGINE_FLOW_HPP
#define CFN_ENGINE_FLOW_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "cfn/core/cancel.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"

namespace cfn {

class CFN_API FlowNetwork {
 public:
  static constexpr std::uint32_t kNoPayload = 0xFFFFFFFFu;

  explicit FlowNetwork(const Limits& limits, CancellationToken cancel = {});

  [[nodiscard]] std::uint32_t add_node();

  /// Adds a directed arc and its residual partner. Returns the forward arc
  /// index, or nullopt when the arc budget is exhausted.
  [[nodiscard]] std::optional<std::uint32_t> add_arc(std::uint32_t from, std::uint32_t to,
                                                     std::uint64_t capacity, std::uint32_t payload);

  /// Finalises adjacency. Must be called before any solve. Safe to call once.
  [[nodiscard]] Outcome<void> build();

  /// Solves on the current residual state, continuing from any flow already
  /// present. Deterministic for a given arc order. Augmentation stops as soon
  /// as the accumulated flow reaches `limit`, which is how a bounded demand is
  /// admitted without over-pushing.
  [[nodiscard]] Outcome<std::uint64_t> max_flow(std::uint32_t source, std::uint32_t sink,
                                                std::uint64_t limit = UINT64_MAX);

  [[nodiscard]] std::uint32_t node_count() const noexcept { return node_count_; }
  [[nodiscard]] std::uint32_t arc_count() const noexcept { return static_cast<std::uint32_t>(arcs_.size()); }

  [[nodiscard]] std::uint64_t arc_capacity(std::uint32_t arc) const noexcept { return arcs_[arc].capacity; }
  [[nodiscard]] std::uint64_t arc_flow(std::uint32_t arc) const noexcept {
    return arcs_[arc].flow > 0 ? static_cast<std::uint64_t>(arcs_[arc].flow) : 0ULL;
  }
  [[nodiscard]] std::uint64_t arc_residual(std::uint32_t arc) const noexcept;
  [[nodiscard]] std::uint32_t arc_tail(std::uint32_t arc) const noexcept { return arcs_[arc].tail; }
  [[nodiscard]] std::uint32_t arc_head(std::uint32_t arc) const noexcept { return arcs_[arc].head; }
  [[nodiscard]] std::uint32_t arc_payload(std::uint32_t arc) const noexcept { return arcs_[arc].payload; }

  void set_arc_capacity(std::uint32_t arc, std::uint64_t capacity) noexcept;
  void reset_flows() noexcept;

  /// Nodes reachable from the supplied source in the residual graph left by the
  /// most recent solve on that source.
  [[nodiscard]] const std::vector<std::uint8_t>& residual_reachable() const noexcept {
    return reachable_;
  }

  [[nodiscard]] std::uint64_t work_units_used() const noexcept { return work_units_; }
  [[nodiscard]] bool built() const noexcept { return built_; }

 private:
  struct Arc {
    std::uint32_t tail = 0;
    std::uint32_t head = 0;
    std::uint32_t reverse = 0;
    std::uint32_t payload = kNoPayload;
    std::uint64_t capacity = 0;
    std::int64_t flow = 0;
  };

  [[nodiscard]] Outcome<void> charge_work(std::uint64_t units);

  Limits limits_;
  CancellationToken cancel_;
  std::vector<Arc> arcs_;
  std::vector<std::uint32_t> out_start_;
  std::vector<std::uint32_t> out_arcs_;
  std::vector<std::int32_t> level_;
  std::vector<std::uint32_t> cursor_;
  std::vector<std::uint8_t> reachable_;
  std::uint32_t node_count_ = 0;
  std::uint64_t work_units_ = 0;
  bool built_ = false;
};

}  // namespace cfn

#endif  // CFN_ENGINE_FLOW_HPP