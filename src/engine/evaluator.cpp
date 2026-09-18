// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/engine/evaluator.hpp"

#include <algorithm>

#include "cfn/core/checked.hpp"

namespace cfn {

namespace {

[[nodiscard]] Outcome<void> require_inputs(const EvaluationInputs& inputs) {
  if (inputs.model == nullptr) {
    return Error(ErrorCode::InvalidArgument, "evaluation has no capacity model");
  }
  if (inputs.policy == nullptr) {
    return Error(ErrorCode::InvalidArgument, "evaluation has no policy");
  }
  if (inputs.resources == nullptr) {
    return Error(ErrorCode::InvalidArgument, "evaluation has no resource catalog");
  }
  if (inputs.topology == nullptr) {
    return Error(ErrorCode::InvalidArgument, "evaluation has no topology");
  }
  if (inputs.reservations == nullptr) {
    return Error(ErrorCode::InvalidArgument, "evaluation has no reservation snapshot");
  }
  if (inputs.degradation == nullptr) {
    return Error(ErrorCode::InvalidArgument, "evaluation has no degradation snapshot");
  }
  if (inputs.domains == nullptr) {
    return Error(ErrorCode::InvalidArgument, "evaluation has no failure domain catalog");
  }
  return Outcome<void>();
}

[[nodiscard]] ConfidenceSummary summarise(const AccountingRollup& rollup,
                                          const FragmentationResult& fragmentation,
                                          std::uint32_t unhealthy_domains) {
  ConfidenceSummary summary;
  summary.present_resource_count = rollup.present_resource_count;
  summary.authoritative_resource_count = rollup.authoritative_resource_count;
  summary.unknown_resource_count = rollup.unknown_resource_count;
  summary.unhealthy_domain_count = unhealthy_domains;
  summary.all_present_resources_authoritative = rollup.unknown_resource_count == 0;
  summary.exact_fit = fragmentation.evaluated && fragmentation.exact;

  std::uint64_t ppm = 1000000ULL;
  if (rollup.present_resource_count != 0 && rollup.unknown_resource_count != 0) {
    const std::uint64_t penalty =
        (static_cast<std::uint64_t>(rollup.unknown_resource_count) * 1000000ULL) /
        static_cast<std::uint64_t>(rollup.present_resource_count);
    ppm = penalty >= ppm ? 0ULL : ppm - penalty;
  }
  if (fragmentation.evaluated && !fragmentation.exact) {
    ppm = (ppm / 4ULL) * 3ULL;
  }
  if (unhealthy_domains != 0) {
    ppm = (ppm / 10ULL) * 9ULL;
  }
  summary.evidence_completeness_ppm = static_cast<std::uint32_t>(ppm);
  return summary;
}

}  // namespace

Outcome<GenerationVector> make_generation_vector(const EvaluationInputs& inputs, Timestamp now,
                                                 FabricEpoch epoch, const Provenance& provenance) {
  CFN_RETURN_IF_ERROR(require_inputs(inputs));
  GenerationVector generations;
  generations.fabric_epoch = epoch;
  generations.model = inputs.model->generation;
  generations.policy = inputs.policy->generation;
  generations.topology = inputs.topology->generation;
  generations.resource_catalog = inputs.resources->generation;
  generations.reservation_snapshot = inputs.reservations->generation;
  generations.failure_domain_catalog = inputs.domains->generation;
  generations.degradation = inputs.degradation->generation;
  generations.demand_shape =
      inputs.demand_shape == nullptr ? Generation{} : inputs.demand_shape->generation;
  generations.resource_set = resource_set_digest(*inputs.resources);
  generations.resource_count = static_cast<std::uint32_t>(inputs.resources->resources.size());
  generations.computed_at = now;
  generations.provenance = provenance;
  return generations;
}

Outcome<CapacitySnapshot> evaluate(const EvaluationInputs& inputs, const EvaluationOptions& options) {
  CFN_RETURN_IF_ERROR(require_inputs(inputs));
  CFN_RETURN_IF_ERROR(options.limits.validate());

  const CapacityModel& model = *inputs.model;
  const CapacityPolicy& policy = *inputs.policy;
  const ResourceCatalog& resources = *inputs.resources;
  const Topology& topology = *inputs.topology;
  const ReservationSnapshot& reservations = *inputs.reservations;
  const DegradationSnapshot& degradation = *inputs.degradation;
  const FailureDomainCatalog& domains = *inputs.domains;

  if (options.cancel.cancelled()) {
    return Error(ErrorCode::Cancelled, "evaluation was cancelled before validation");
  }

  CFN_RETURN_IF_ERROR(validate(model, options.limits));
  CFN_RETURN_IF_ERROR(validate(policy, options.limits));
  CFN_RETURN_IF_ERROR(validate(resources, options.limits));
  CFN_RETURN_IF_ERROR(validate(topology, resources, options.limits));
  CFN_RETURN_IF_ERROR(validate(reservations, resources, options.limits));
  CFN_RETURN_IF_ERROR(validate(domains, options.limits));
  CFN_RETURN_IF_ERROR(validate(degradation, resources, domains, options.limits));

  if (model.policy != policy.id) {
    return Error(ErrorCode::NotFound, "capacity model names a policy that was not supplied",
                 model.policy.view());
  }
  if (model.policy_generation != policy.generation) {
    return Error(ErrorCode::StalePolicy, "capacity model is bound to a different policy generation",
                 model.policy.view());
  }

  const DemandShape* shape = nullptr;
  if (model.has_demand_shape()) {
    if (inputs.demand_shape == nullptr) {
      return Error(ErrorCode::NotFound, "capacity model names a demand shape that was not supplied",
                   model.demand_shape.view());
    }
    if (inputs.demand_shape->id != model.demand_shape) {
      return Error(ErrorCode::StaleDemandShape, "supplied demand shape is not the one the model names",
                   model.demand_shape.view());
    }
    if (inputs.demand_shape->generation != model.demand_shape_generation) {
      return Error(ErrorCode::StaleDemandShape,
                   "capacity model is bound to a different demand shape generation",
                   model.demand_shape.view());
    }
    CFN_RETURN_IF_ERROR(validate(*inputs.demand_shape, resources, options.limits));
    shape = inputs.demand_shape;
  } else if (inputs.demand_shape != nullptr) {
    return Error(ErrorCode::Contradictory,
                 "a demand shape was supplied for a model that binds none",
                 inputs.demand_shape->id.view());
  }

  const Outcome<GenerationVector> generations =
      make_generation_vector(inputs, options.now, options.epoch, options.provenance);
  if (!generations) {
    return generations.error();
  }

  Outcome<std::vector<ResourceAccounting>> derived =
      derive_accounting(resources, topology, reservations, degradation, domains, policy, options.limits);
  if (!derived) {
    return derived.error();
  }
  if (options.cancel.cancelled()) {
    return Error(ErrorCode::Cancelled, "evaluation was cancelled after accounting");
  }

  Outcome<AccountingRollup> rollup = roll_up(*derived);
  if (!rollup) {
    return rollup.error();
  }

  FragmentationResult fragmentation;
  if (shape != nullptr && options.evaluate_fragmentation) {
    Outcome<FragmentationResult> analysis = analyse_fragmentation(*derived, topology, policy, shape,
                                                                  options.limits, options.cancel);
    if (!analysis) {
      return analysis.error();
    }
    fragmentation = *analysis;
    rollup->usable_total = fragmentation.satisfied;
    rollup->spare_total = fragmentation.spare;
    rollup->stranded_total = fragmentation.stranding.total();
    rollup->stranding = fragmentation.stranding;
    rollup->reserved_stranded = fragmentation.reserved_stranded;
    rollup->fragmentation_evaluated = true;
  }

  CFN_RETURN_IF_ERROR(verify_closure(*rollup));

  CapacitySnapshot snapshot;
  snapshot.model = model.id;
  snapshot.generations = *generations;
  snapshot.rollup = *rollup;
  snapshot.fragmentation = fragmentation;
  snapshot.per_resource = std::move(*derived);
  snapshot.provenance = options.provenance;

  std::uint32_t unhealthy = 0;
  for (const FailureDomainRecord& record : domains.domains) {
    if (!record.healthy) {
      unhealthy += 1U;
    }
  }
  snapshot.confidence = summarise(snapshot.rollup, snapshot.fragmentation, unhealthy);

  if (options.snapshot_id.valid()) {
    snapshot.id = options.snapshot_id;
  } else {
    snapshot.id = make_snapshot_id(model.id, snapshot.generations);
  }
  snapshot.generation = options.snapshot_generation.valid() ? options.snapshot_generation
                                                            : Generation::initial();

  CFN_RETURN_IF_ERROR(verify_closure(snapshot));
  snapshot.closure_checks = 2;
  snapshot.closure_verified = true;
  if (options.cancel.cancelled()) {
    // A cancelled evaluation must not publish a result.
    return Error(ErrorCode::Cancelled, "evaluation was cancelled before it completed");
  }
  return snapshot;
}

}  // namespace cfn
