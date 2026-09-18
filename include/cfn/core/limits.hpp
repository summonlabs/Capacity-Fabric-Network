// Capacity Fabric Network - resource bounds.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Every unbounded quantity in the library - collection sizes, payload sizes,
// computation budgets, durable growth - is governed by a Limits value. A
// rejected limit is an error, never a silent truncation.
#ifndef CFN_CORE_LIMITS_HPP
#define CFN_CORE_LIMITS_HPP

#include <cstdint>

#include "cfn/core/outcome.hpp"

namespace cfn {

struct Limits {
  // --- Model population -----------------------------------------------------
  std::uint32_t max_resources = 200000;
  std::uint32_t max_topology_nodes = 200000;
  std::uint32_t max_topology_edges = 800000;
  std::uint32_t max_failure_domains = 65536;
  std::uint32_t max_reservations = 200000;
  std::uint32_t max_degradation_records = 200000;
  std::uint32_t max_demand_flows = 8192;
  std::uint32_t max_models = 4096;
  std::uint32_t max_policies = 4096;
  std::uint32_t max_demand_shapes = 4096;

  // --- Values ---------------------------------------------------------------
  std::uint64_t max_capacity_units = 1ULL << 62;
  std::uint32_t max_priority = 1000000;

  // --- Flow / fragmentation -------------------------------------------------
  /// Upper bound on Dinic work (arc relaxations) for one max-flow solve. It is
  /// a backstop that turns a pathological or adversarial graph into a rejection
  /// instead of an unbounded run, not a performance target: a legitimate
  /// population with tens of thousands of resources needs hundreds of millions
  /// of relaxations. Cancellation is checked far more often than this bound.
  std::uint64_t max_flow_work_units = 20000000000ULL;
  std::uint32_t max_path_length = 8192;
  /// Failure domains examined by an N-1 or N-1-1 resilience evaluation.
  std::uint32_t max_resilience_domains_scanned = 4096;
  std::uint32_t max_bottleneck_resources = 256;

  // --- Prediction -----------------------------------------------------------
  std::uint32_t max_prediction_samples = 4096;
  std::uint32_t max_prediction_assumptions = 64;

  // --- Explanation ----------------------------------------------------------
  std::uint32_t max_explanation_entries = 4096;
  std::uint32_t max_diagnostics = 512;

  // --- Persistence ----------------------------------------------------------
  std::uint32_t max_journal_record_bytes = 1u << 20;
  std::uint64_t max_journal_bytes = 512ULL << 20;
  std::uint64_t max_journal_records = 4000000;
  std::uint32_t max_history_entries = 65536;

  // --- Transport ------------------------------------------------------------
  std::uint32_t max_frame_bytes = 1u << 20;
  std::uint32_t max_evidence_records_per_response = 8192;
  std::uint32_t max_connections = 32;

  /// Structural sanity: every bound must be usable and internally consistent.
  [[nodiscard]] CFN_API Outcome<void> validate() const;

  [[nodiscard]] friend bool operator==(const Limits&, const Limits&) noexcept = default;
};

[[nodiscard]] CFN_API const Limits& default_limits();

}  // namespace cfn

#endif  // CFN_CORE_LIMITS_HPP