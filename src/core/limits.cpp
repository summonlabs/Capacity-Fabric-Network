// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/core/limits.hpp"

namespace cfn {

namespace {

[[nodiscard]] Outcome<void> require_nonzero(std::uint64_t value, const char* name) {
  if (value == 0) {
    return Error(ErrorCode::InvalidArgument, "limit must be positive", name);
  }
  return Outcome<void>();
}

}  // namespace

Outcome<void> Limits::validate() const {
  CFN_RETURN_IF_ERROR(require_nonzero(max_resources, "max_resources"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_topology_nodes, "max_topology_nodes"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_topology_edges, "max_topology_edges"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_failure_domains, "max_failure_domains"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_reservations, "max_reservations"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_degradation_records, "max_degradation_records"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_demand_flows, "max_demand_flows"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_models, "max_models"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_policies, "max_policies"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_demand_shapes, "max_demand_shapes"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_priority, "max_priority"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_flow_work_units, "max_flow_work_units"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_path_length, "max_path_length"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_resilience_domains_scanned, "max_resilience_domains_scanned"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_bottleneck_resources, "max_bottleneck_resources"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_prediction_samples, "max_prediction_samples"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_prediction_assumptions, "max_prediction_assumptions"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_explanation_entries, "max_explanation_entries"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_diagnostics, "max_diagnostics"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_journal_record_bytes, "max_journal_record_bytes"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_journal_bytes, "max_journal_bytes"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_journal_records, "max_journal_records"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_history_entries, "max_history_entries"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_frame_bytes, "max_frame_bytes"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_evidence_records_per_response, "max_evidence_records_per_response"));
  CFN_RETURN_IF_ERROR(require_nonzero(max_connections, "max_connections"));

  if (max_capacity_units == 0) {
    return Error(ErrorCode::InvalidArgument, "max_capacity_units must be positive", "max_capacity_units");
  }
  if (max_capacity_units > (1ULL << 62)) {
    return Error(ErrorCode::InvalidArgument, "max_capacity_units exceeds the supported range",
                 "max_capacity_units");
  }
  if (max_journal_record_bytes < 32) {
    return Error(ErrorCode::InvalidArgument, "max_journal_record_bytes is too small for a record header",
                 "max_journal_record_bytes");
  }
  if (max_frame_bytes < 16) {
    return Error(ErrorCode::InvalidArgument, "max_frame_bytes is too small for a frame header",
                 "max_frame_bytes");
  }
  if (max_history_entries > 1000000) {
    return Error(ErrorCode::InvalidArgument, "max_history_entries is unreasonably large",
                 "max_history_entries");
  }
  return Outcome<void>();
}

const Limits& default_limits() {
  static const Limits limits{};
  return limits;
}

}  // namespace cfn
