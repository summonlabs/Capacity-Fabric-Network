// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/explain/explanation.hpp"

#include <algorithm>
#include <map>
#include <string>

#include "cfn/core/checked.hpp"
#include "cfn/core/text.hpp"
#include "cfn/engine/invalidation.hpp"
#include "cfn/text/json.hpp"

namespace cfn {

namespace {

struct Builder {
  Explanation explanation;
  Limits limits;
  std::size_t limit = 0;
  bool truncated = false;

  void add(std::string_view section, std::string_view key, std::string value) {
    if (explanation.fields.size() >= limit) {
      truncated = true;
      return;
    }
    ExplanationField field;
    field.section = std::string(section);
    field.key = std::string(key);
    field.value = std::move(value);
    explanation.fields.push_back(std::move(field));
  }

  void add_u64(std::string_view section, std::string_view key, std::uint64_t value) {
    add(section, key, text::format_u64(value));
  }
};

[[nodiscard]] const char* evidence_class_name(EvidenceClass value) {
  switch (value) {
    case EvidenceClass::Unknown: return "unknown";
    case EvidenceClass::Declared: return "declared";
    case EvidenceClass::Measured: return "measured";
    case EvidenceClass::Derived: return "derived";
    case EvidenceClass::Predicted: return "predicted";
    case EvidenceClass::Imported: return "imported";
  }
  return "unknown";
}

}  // namespace

const std::string* Explanation::find(std::string_view section, std::string_view key) const noexcept {
  for (const ExplanationField& field : fields) {
    if (field.section == section && field.key == key) {
      return &field.value;
    }
  }
  return nullptr;
}

std::string Explanation::to_text() const {
  std::string out = title;
  out.push_back('\n');
  std::string current;
  for (const ExplanationField& field : fields) {
    if (field.section != current) {
      current = field.section;
      out.push_back('\n');
      out.append("[");
      out.append(current);
      out.append("]\n");
    }
    out.append("  ");
    out.append(field.key);
    std::size_t pad = field.key.size() < 28 ? 28 - field.key.size() : 1;
    out.append(pad, ' ');
    out.append(field.value);
    out.push_back('\n');
  }
  return out;
}

std::string Explanation::to_json() const {
  text::JsonWriter writer(true);
  writer.begin_object();
  writer.field("title", title);
  writer.key("sections");
  writer.begin_object();
  std::string current;
  for (const ExplanationField& field : fields) {
    if (field.section != current) {
      if (!current.empty()) {
        writer.end_object();
      }
      current = field.section;
      writer.key(current);
      writer.begin_object();
    }
    writer.field(field.key, field.value);
  }
  if (!current.empty()) {
    writer.end_object();
  }
  writer.end_object();
  writer.end_object();
  return writer.take();
}

Outcome<Explanation> explain(const CapacitySnapshot& snapshot, const Limits& limits) {
  if (limits.max_explanation_entries == 0) {
    return Error(ErrorCode::LimitExceeded, "explanation budget is zero");
  }
  Builder builder;
  builder.limits = limits;
  builder.limit = limits.max_explanation_entries;
  builder.explanation.title = std::string("capacity snapshot ").append(snapshot.id.view());

  builder.add("identity", "snapshot", std::string(snapshot.id.view()));
  builder.add("identity", "model", std::string(snapshot.model.view()));
  builder.add_u64("identity", "snapshot_generation", snapshot.generation.value());
  builder.add("identity", "computed_at", to_iso8601(snapshot.generations.computed_at));

  builder.add_u64("generations", "fabric_epoch", snapshot.generations.fabric_epoch.value());
  builder.add_u64("generations", "model", snapshot.generations.model.value());
  builder.add_u64("generations", "policy", snapshot.generations.policy.value());
  builder.add_u64("generations", "topology", snapshot.generations.topology.value());
  builder.add_u64("generations", "resource_catalog", snapshot.generations.resource_catalog.value());
  builder.add_u64("generations", "resource_set_digest", snapshot.generations.resource_set.value);
  builder.add_u64("generations", "resource_count", snapshot.generations.resource_count);
  builder.add_u64("generations", "reservation_snapshot",
                  snapshot.generations.reservation_snapshot.value());
  builder.add_u64("generations", "failure_domain_catalog",
                  snapshot.generations.failure_domain_catalog.value());
  builder.add_u64("generations", "degradation", snapshot.generations.degradation.value());
  builder.add_u64("generations", "demand_shape", snapshot.generations.demand_shape.value());

  const AccountingRollup& rollup = snapshot.rollup;
  builder.add_u64("accounting", "raw", rollup.raw_total.units);
  builder.add_u64("accounting", "unknown", rollup.unknown_total.units);
  builder.add_u64("accounting", "reserved", rollup.reserved_total.units);
  builder.add_u64("accounting", "protected_headroom", rollup.headroom_total.units);
  builder.add_u64("accounting", "degraded", rollup.degraded_total.units);
  builder.add_u64("accounting", "available", rollup.available_total.units);
  builder.add_u64("accounting", "usable", rollup.usable_total.units);
  builder.add_u64("accounting", "spare", rollup.spare_total.units);
  builder.add_u64("accounting", "stranded", rollup.stranded_total.units);
  builder.add_u64("accounting", "reserved_but_stranded", rollup.reserved_stranded.units);
  builder.add_u64("accounting", "resources", rollup.resource_count);
  builder.add_u64("accounting", "present_resources", rollup.present_resource_count);
  builder.add_u64("accounting", "authoritative_resources", rollup.authoritative_resource_count);
  builder.add_u64("accounting", "unknown_resources", rollup.unknown_resource_count);

  builder.add_u64("stranding", "segmentation", rollup.stranding.segmentation.units);
  builder.add_u64("stranding", "bottleneck", rollup.stranding.bottleneck.units);
  builder.add_u64("stranding", "failure_domain_resilience",
                  rollup.stranding.failure_domain_resilience.units);

  const FragmentationResult& fragmentation = snapshot.fragmentation;
  builder.add("fragmentation", "evaluated", fragmentation.evaluated ? "true" : "false");
  builder.add("fragmentation", "exact", fragmentation.exact ? "true" : "false");
  builder.add_u64("fragmentation", "deliverable", fragmentation.deliverable.units);
  builder.add_u64("fragmentation", "deliverable_before_resilience",
                  fragmentation.deliverable_unresilient.units);
  builder.add_u64("fragmentation", "satisfied", fragmentation.satisfied.units);
  builder.add_u64("fragmentation", "satisfied_upper_bound", fragmentation.satisfied_upper.units);
  builder.add_u64("fragmentation", "granularity_loss", fragmentation.granularity_loss.units);
  builder.add_u64("fragmentation", "resilience_scenarios", fragmentation.resilience_scenarios);
  builder.add("fragmentation", "resilience_applied",
              fragmentation.resilience_applied ? "true" : "false");
  builder.add_u64("fragmentation", "flow_count", fragmentation.flows.size());
  for (const FlowFit& flow : fragmentation.flows) {
    if (builder.explanation.fields.size() >= builder.limit) {
      builder.truncated = true;
      break;
    }
    std::string key = std::string("flow ").append(flow.id.view());
    std::string value = "requested=";
    value.append(text::format_u64(flow.requested.units));
    value.append(" admitted=");
    value.append(text::format_u64(flow.admitted.units));
    value.append(" unmet=");
    value.append(text::format_u64(flow.unmet.units));
    value.append(flow.satisfied ? " satisfied" : " partial");
    builder.add("fragmentation", key, std::move(value));
  }

  for (const ResourceId& id : fragmentation.bottleneck_resources) {
    if (builder.explanation.fields.size() >= builder.limit) {
      builder.truncated = true;
      break;
    }
    const ResourceAccounting* row = snapshot.find(id);
    std::string value = "unused=";
    value.append(row == nullptr ? "0" : text::format_u64(row->stranded.units));
    builder.add("bottlenecks", std::string(id.view()), std::move(value));
  }
  for (const ResourceId& id : fragmentation.segmentation_resources) {
    if (builder.explanation.fields.size() >= builder.limit) {
      builder.truncated = true;
      break;
    }
    const ResourceAccounting* row = snapshot.find(id);
    std::string value = "available=";
    value.append(row == nullptr ? "0" : text::format_u64(row->available.units));
    value.append(" cause=segmentation");
    builder.add("segmented", std::string(id.view()), std::move(value));
  }

  builder.add_u64("confidence", "present_resources", snapshot.confidence.present_resource_count);
  builder.add_u64("confidence", "authoritative_resources",
                  snapshot.confidence.authoritative_resource_count);
  builder.add_u64("confidence", "unknown_resources", snapshot.confidence.unknown_resource_count);
  builder.add_u64("confidence", "unhealthy_domains", snapshot.confidence.unhealthy_domain_count);
  builder.add_u64("confidence", "evidence_completeness_ppm",
                  snapshot.confidence.evidence_completeness_ppm);
  builder.add("confidence", "all_present_authoritative",
              snapshot.confidence.all_present_resources_authoritative ? "true" : "false");
  builder.add("confidence", "fit_exact", snapshot.confidence.exact_fit ? "true" : "false");

  builder.add("closure", "verified", snapshot.closure_verified ? "true" : "false");
  builder.add_u64("closure", "checks", snapshot.closure_checks);

  builder.add("provenance", "evidence_class", evidence_class_name(snapshot.provenance.evidence_class));
  builder.add("provenance", "authoritative", snapshot.provenance.authoritative ? "true" : "false");
  builder.add("provenance", "observed_at", to_iso8601(snapshot.provenance.observed_at));
  if (builder.truncated) {
    builder.add("limits", "truncated", "the explanation reached its configured entry budget");
  }
  return builder.explanation;
}

Outcome<Explanation> explain(const PredictionResult& prediction, const Limits& limits) {
  if (limits.max_explanation_entries == 0) {
    return Error(ErrorCode::LimitExceeded, "explanation budget is zero");
  }
  Builder builder;
  builder.limits = limits;
  builder.limit = limits.max_explanation_entries;
  builder.explanation.title = "capacity prediction";

  builder.add("identity", "model", std::string(to_string(prediction.kind)));
  builder.add("identity", "produced_at", to_iso8601(prediction.produced_at));
  builder.add("identity", "horizon", to_iso8601(prediction.horizon));
  builder.add("boundary", "is_observation", "false");
  builder.add("boundary", "evidence_class", evidence_class_name(prediction.evidence_class));
  builder.add("boundary", "usable_as_observed_capacity", "false");
  builder.add("result", "available", prediction.available ? "true" : "false");
  if (!prediction.available) {
    builder.add("result", "reason", prediction.unavailable_reason);
  }
  builder.add_u64("result", "predicted_available", prediction.predicted_available.units);
  builder.add_u64("result", "base_observed", prediction.base_observed.units);
  builder.add_u64("result", "confidence_ppm", prediction.confidence_ppm);
  builder.add_u64("result", "series_count", prediction.per_resource.size());
  for (const ResourcePrediction& series : prediction.per_resource) {
    if (builder.explanation.fields.size() >= builder.limit) {
      builder.truncated = true;
      break;
    }
    std::string value = "available=";
    value.append(series.available ? "true" : "false");
    value.append(" samples=");
    value.append(text::format_u64(series.sample_count));
    value.append(" predicted=");
    value.append(text::format_u64(series.predicted_available.units));
    value.append(" slope_per_hour=");
    value.append(text::format_i64(series.slope_per_hour));
    value.append(" confidence_ppm=");
    value.append(text::format_u64(series.confidence_ppm));
    builder.add("series", std::string(series.resource.view()), std::move(value));
  }
  for (const std::string& assumption : prediction.assumptions) {
    if (builder.explanation.fields.size() >= builder.limit) {
      builder.truncated = true;
      break;
    }
    builder.add("assumptions", "assumption", assumption);
  }
  if (builder.truncated) {
    builder.add("limits", "truncated", "the explanation reached its configured entry budget");
  }
  return builder.explanation;
}

Outcome<Explanation> explain(const FitQuery& query, const FitQueryResult& result, const Limits& limits) {
  if (limits.max_explanation_entries == 0) {
    return Error(ErrorCode::LimitExceeded, "explanation budget is zero");
  }
  Builder builder;
  builder.limits = limits;
  builder.limit = limits.max_explanation_entries;
  builder.explanation.title = "capacity fit query";

  builder.add("query", "source", std::string(query.source.view()));
  builder.add("query", "sink", std::string(query.sink.view()));
  builder.add_u64("query", "magnitude", query.magnitude.units);
  builder.add_u64("query", "granularity", query.granularity.units);
  builder.add("query", "resilience", std::string(to_string(query.resilience)));
  builder.add("result", "satisfiable", result.satisfiable ? "true" : "false");
  builder.add("result", "exact", result.exact ? "true" : "false");
  builder.add_u64("result", "admitted", result.admitted.units);
  builder.add_u64("result", "deliverable", result.deliverable.units);
  builder.add_u64("result", "bottleneck_capacity", result.bottleneck_capacity.units);
  builder.add_u64("result", "resilience_scenarios", result.resilience_scenarios);
  builder.add_u64("stranding", "segmentation", result.stranding.segmentation.units);
  builder.add_u64("stranding", "bottleneck", result.stranding.bottleneck.units);
  builder.add_u64("stranding", "failure_domain_resilience",
                  result.stranding.failure_domain_resilience.units);
  for (const ResourceId& id : result.bottleneck_resources) {
    if (builder.explanation.fields.size() >= builder.limit) {
      builder.truncated = true;
      break;
    }
    builder.add("bottlenecks", std::string(id.view()), "constrained");
  }
  for (const ResourceId& id : result.segmentation_resources) {
    if (builder.explanation.fields.size() >= builder.limit) {
      builder.truncated = true;
      break;
    }
    builder.add("segmented", std::string(id.view()), "off-path");
  }
  if (builder.truncated) {
    builder.add("limits", "truncated", "the explanation reached its configured entry budget");
  }
  return builder.explanation;
}

Outcome<Explanation> explain_rejection(const CapacitySnapshot& snapshot, const GenerationVector& current,
                                       const Error& error, const Limits& limits) {
  if (limits.max_explanation_entries == 0) {
    return Error(ErrorCode::LimitExceeded, "explanation budget is zero");
  }
  Builder builder;
  builder.limits = limits;
  builder.limit = limits.max_explanation_entries;
  builder.explanation.title = std::string("snapshot rejected: ").append(snapshot.id.view());
  builder.add("rejection", "code", std::string(to_string(error.code())));
  builder.add("rejection", "message", std::string(error.message()));
  builder.add("rejection", "subject", std::string(error.subject()));
  builder.add("rejection", "staleness",
              std::string(to_string(classify_staleness(snapshot, current))));
  builder.add_u64("bound", "fabric_epoch", snapshot.generations.fabric_epoch.value());
  builder.add_u64("bound", "topology", snapshot.generations.topology.value());
  builder.add_u64("bound", "resource_catalog", snapshot.generations.resource_catalog.value());
  builder.add_u64("bound", "reservation_snapshot",
                  snapshot.generations.reservation_snapshot.value());
  builder.add_u64("bound", "failure_domain_catalog",
                  snapshot.generations.failure_domain_catalog.value());
  builder.add_u64("bound", "degradation", snapshot.generations.degradation.value());
  builder.add_u64("current", "fabric_epoch", current.fabric_epoch.value());
  builder.add_u64("current", "topology", current.topology.value());
  builder.add_u64("current", "resource_catalog", current.resource_catalog.value());
  builder.add_u64("current", "reservation_snapshot", current.reservation_snapshot.value());
  builder.add_u64("current", "failure_domain_catalog", current.failure_domain_catalog.value());
  builder.add_u64("current", "degradation", current.degradation.value());
  return builder.explanation;
}

}  // namespace cfn