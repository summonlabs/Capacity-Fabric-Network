// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/fabric/fabric.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <utility>

namespace cfn {

namespace {

[[nodiscard]] Provenance default_provenance(Clock& clock) {
  const auto source = EvidenceSourceId::parse("cfn-fabric");
  if (!source.has_value()) {
    return Provenance{};
  }
  return declared_provenance(*source, clock.wall_now(), Duration{});
}

[[nodiscard]] Outcome<CapacityPolicy> policy_for(const RegistryView& view, const PolicyId& id) {
  const auto found = view.policies.find(id);
  if (found == view.policies.end()) {
    return Error(ErrorCode::NotFound, "policy is not registered", id.view());
  }
  return found->second;
}

[[nodiscard]] Outcome<const DemandShape*> shape_for(const RegistryView& view,
                                                    const CapacityModel& model) {
  if (!model.has_demand_shape()) {
    return static_cast<const DemandShape*>(nullptr);
  }
  const auto found = view.demand_shapes.find(model.demand_shape);
  if (found == view.demand_shapes.end()) {
    return Error(ErrorCode::NotFound, "demand shape is not registered", model.demand_shape.view());
  }
  return &found->second;
}

[[nodiscard]] Outcome<void> require_inputs(const RegistryView& view) {
  if (!view.has_resource_catalog) {
    return Error(ErrorCode::InvalidState, "no authoritative resource catalog is registered");
  }
  if (!view.has_topology) {
    return Error(ErrorCode::InvalidState, "no authoritative topology is registered");
  }
  if (!view.has_failure_domains) {
    return Error(ErrorCode::InvalidState, "no authoritative failure domain catalog is registered");
  }
  return Outcome<void>();
}

[[nodiscard]] Outcome<void> require_fresh(const RegistryView& view, Timestamp now) {
  const Provenance* sources[] = {&view.resources.provenance, &view.topology.provenance,
                                 &view.reservations.provenance, &view.degradation.provenance,
                                 &view.domains.provenance};
  for (const Provenance* provenance : sources) {
    if (!provenance->source_id.valid()) {
      return Error(ErrorCode::StaleEvidence,
                   "an authoritative input carries no provenance and cannot be trusted");
    }
    if (!is_fresh(*provenance, now)) {
      return Error(ErrorCode::StaleEvidence,
                   "an authoritative input is outside its declared freshness window",
                   provenance->source_id.view());
    }
  }
  return Outcome<void>();
}

}  // namespace

Fabric::Fabric(const FabricOptions& options, FabricEpoch epoch, RegistryViewPtr initial,
               RecoveryReport report)
    : limits_(options.limits),
      clock_(options.clock ? options.clock : make_system_clock()),
      provenance_(options.provenance),
      registry_(options.limits, epoch, options.provenance),
      store_(nullptr),
      recovery_(std::move(report)),
      durable_(false),
      require_fresh_evidence_(options.require_fresh_evidence) {
  if (initial) {
    (void)registry_.adopt(std::move(*initial));
  }
}

Fabric::~Fabric() { (void)shutdown(); }

Outcome<std::unique_ptr<Fabric>> Fabric::open(const FabricOptions& options) {
  Limits limits = options.limits;
  CFN_RETURN_IF_ERROR(limits.validate());

  const std::shared_ptr<Clock> clock = options.clock ? options.clock : make_system_clock();
  Provenance provenance = options.provenance;
  if (!provenance.source_id.valid()) {
    provenance = default_provenance(*clock);
  }
  CFN_RETURN_IF_ERROR(validate(provenance));

  FabricOptions resolved = options;
  resolved.limits = limits;
  resolved.clock = clock;
  resolved.provenance = provenance;

  RecoveryReport report;
  std::unique_ptr<CapacityStore> store;
  FabricEpoch epoch = FabricEpoch::initial();

  if (!options.store_directory.empty()) {
    StoreOptions store_options;
    store_options.directory = options.store_directory;
    store_options.limits = limits;
    store_options.allow_state_discard_on_corruption = options.allow_state_discard_on_corruption;
    store_options.durability_barrier = true;
    Outcome<std::unique_ptr<CapacityStore>> opened =
        CapacityStore::open(store_options, *clock, report);
    if (!opened) {
      return opened.error();
    }
    store = std::move(*opened);
    epoch = store->epoch();
  } else {
    report.opened = true;
    report.durable = false;
    report.requires_revalidation = true;
    report.diagnostics.emplace_back(
        "the fabric was opened without a durable store; configuration is not persistent");
  }

  std::unique_ptr<Fabric> fabric(new Fabric(resolved, epoch, nullptr, report));
  fabric->store_ = std::move(store);
  fabric->durable_ = fabric->store_ != nullptr;

  if (fabric->store_ != nullptr) {
    RegistryView restored;
    restored.epoch = epoch;
    restored.provenance = provenance;
    restored.domains.generation = Generation::initial();
    restored.domains.epoch = epoch;
    restored.domains.provenance = provenance;
    restored.reservations.generation = Generation::initial();
    restored.reservations.epoch = epoch;
    restored.reservations.provenance = provenance;
    restored.degradation.generation = Generation::initial();
    restored.degradation.epoch = epoch;
    restored.degradation.provenance = provenance;
    restored.topology.generation = Generation::initial();
    restored.topology.epoch = epoch;
    restored.topology.provenance = provenance;
    restored.resources.generation = Generation::initial();
    restored.resources.epoch = epoch;
    restored.resources.provenance = provenance;
    restored.policies = fabric->store_->policies();
    restored.demand_shapes = fabric->store_->demand_shapes();
    restored.models = fabric->store_->models();
    CFN_RETURN_IF_ERROR(fabric->registry_.adopt(std::move(restored)));
    fabric->history_ = fabric->store_->history();
  }
  return fabric;
}

FabricEpoch Fabric::epoch() const { return registry_.epoch(); }
const RecoveryReport& Fabric::recovery() const noexcept { return recovery_; }
bool Fabric::durable() const noexcept { return durable_; }

Outcome<void> Fabric::set_resource_catalog(ResourceCatalog catalog) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.set_resource_catalog(std::move(catalog));
}

Outcome<void> Fabric::upsert_resource(ResourceRecord record) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.upsert_resource(std::move(record));
}

Outcome<void> Fabric::remove_resource(const ResourceId& id) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.remove_resource(id);
}

Outcome<void> Fabric::set_topology(Topology topology) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.set_topology(std::move(topology));
}

Outcome<void> Fabric::set_reservations(ReservationSnapshot snapshot) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.set_reservations(std::move(snapshot));
}

Outcome<void> Fabric::set_failure_domains(FailureDomainCatalog catalog) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.set_failure_domains(std::move(catalog));
}

Outcome<void> Fabric::set_degradation(DegradationSnapshot snapshot) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.set_degradation(std::move(snapshot));
}

Outcome<void> Fabric::register_policy(CapacityPolicy policy) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  CFN_RETURN_IF_ERROR(registry_.put_policy(policy));
  if (store_ != nullptr) {
    const RegistryViewPtr view = registry_.view();
    const auto stored = view->policies.find(policy.id);
    if (stored != view->policies.end()) {
      CFN_RETURN_IF_ERROR(store_->put_policy(stored->second));
    }
  }
  return Outcome<void>();
}

Outcome<void> Fabric::register_demand_shape(DemandShape shape) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  CFN_RETURN_IF_ERROR(registry_.put_demand_shape(shape));
  if (store_ != nullptr) {
    const RegistryViewPtr view = registry_.view();
    const auto stored = view->demand_shapes.find(shape.id);
    if (stored != view->demand_shapes.end()) {
      CFN_RETURN_IF_ERROR(store_->put_demand_shape(stored->second));
    }
  }
  return Outcome<void>();
}

Outcome<void> Fabric::register_model(CapacityModel model) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  CFN_RETURN_IF_ERROR(registry_.put_model(model));
  if (store_ != nullptr) {
    const RegistryViewPtr view = registry_.view();
    const auto stored = view->models.find(model.id);
    if (stored != view->models.end()) {
      CFN_RETURN_IF_ERROR(store_->put_model(stored->second));
    }
  }
  return Outcome<void>();
}

Outcome<void> Fabric::remove_model(const CapacityModelId& id) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  CFN_RETURN_IF_ERROR(registry_.remove_model(id));
  if (store_ != nullptr) {
    CFN_RETURN_IF_ERROR(store_->remove_model(id));
  }
  return Outcome<void>();
}

Outcome<CapacitySnapshot> Fabric::evaluate_view(const RegistryView& view, const CapacityModel& model,
                                                Timestamp now) const {
  const Outcome<CapacityPolicy> policy = policy_for(view, model.policy);
  if (!policy) {
    return policy.error();
  }
  const Outcome<const DemandShape*> shape = shape_for(view, model);
  if (!shape) {
    return shape.error();
  }
  CFN_RETURN_IF_ERROR(require_inputs(view));
  if (require_fresh_evidence_) {
    CFN_RETURN_IF_ERROR(require_fresh(view, now));
  }

  EvaluationInputs inputs;
  inputs.model = &model;
  inputs.policy = &*policy;
  inputs.resources = &view.resources;
  inputs.topology = &view.topology;
  inputs.reservations = &view.reservations;
  inputs.degradation = &view.degradation;
  inputs.domains = &view.domains;
  inputs.demand_shape = *shape;

  EvaluationOptions options;
  options.limits = limits_;
  options.now = now;
  options.epoch = view.epoch;
  options.provenance = view.provenance.source_id.valid() ? view.provenance : provenance_;
  return evaluate(inputs, options);
}

Outcome<CapacitySnapshot> Fabric::compute(const CapacityModelId& model) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  const RegistryViewPtr view = registry_.view();
  const auto found = view->models.find(model);
  if (found == view->models.end()) {
    return Error(ErrorCode::NotFound, "capacity model is not registered", model.view());
  }
  const CapacityModel model_copy = found->second;
  const Timestamp now = clock_->wall_now();

  CancellationToken token;
  {
    const std::lock_guard<std::mutex> guard(lifecycle_mutex_);
    if (shutting_down_.load(std::memory_order_acquire)) {
      return Error(ErrorCode::Closed, "the fabric is shutting down");
    }
    active_tokens_.push_back(token);
    active_evaluations_.fetch_add(1, std::memory_order_acq_rel);
  }

  Outcome<CapacitySnapshot> snapshot = evaluate_view(*view, model_copy, now);

  {
    const std::lock_guard<std::mutex> guard(lifecycle_mutex_);
    active_tokens_.erase(std::remove_if(active_tokens_.begin(), active_tokens_.end(),
                                        [&](const CancellationToken& candidate) {
                                          return candidate.identity() == token.identity();
                                        }),
                         active_tokens_.end());
    active_evaluations_.fetch_sub(1, std::memory_order_acq_rel);
  }
  lifecycle_cv_.notify_all();

  if (shutting_down_.load(std::memory_order_acquire)) {
    rejected_evaluations_.fetch_add(1, std::memory_order_relaxed);
    return Error(ErrorCode::Cancelled,
                 "the fabric shut down while the evaluation was running; no result was published");
  }
  if (!snapshot) {
    rejected_evaluations_.fetch_add(1, std::memory_order_relaxed);
    return snapshot.error();
  }
  publish_snapshot(*snapshot);
  completed_evaluations_.fetch_add(1, std::memory_order_relaxed);
  return snapshot;
}

Outcome<CapacitySnapshot> Fabric::compute_ad_hoc(const CapacityPolicy& policy,
                                                          const DemandShape* shape) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  const RegistryViewPtr view = registry_.view();
  CFN_RETURN_IF_ERROR(require_inputs(*view));
  if (require_fresh_evidence_) {
    CFN_RETURN_IF_ERROR(require_fresh(*view, clock_->wall_now()));
  }
  CFN_RETURN_IF_ERROR(validate(policy, limits_));
  if (shape != nullptr) {
    CFN_RETURN_IF_ERROR(validate(*shape, view->resources, limits_));
  }

  CapacityModel model;
  const auto model_id = CapacityModelId::parse("ad-hoc-model");
  if (!model_id.has_value()) {
    return Error(ErrorCode::InvalidState, "ad hoc model identity template is invalid");
  }
  model.id = *model_id;
  model.generation = Generation::initial();
  model.policy = policy.id;
  model.policy_generation = policy.generation;
  model.epoch = view->epoch;
  model.provenance = view->provenance.source_id.valid() ? view->provenance : provenance_;
  if (shape != nullptr) {
    model.demand_shape = shape->id;
    model.demand_shape_generation = shape->generation;
  }

  EvaluationInputs inputs;
  inputs.model = &model;
  inputs.policy = &policy;
  inputs.resources = &view->resources;
  inputs.topology = &view->topology;
  inputs.reservations = &view->reservations;
  inputs.degradation = &view->degradation;
  inputs.domains = &view->domains;
  inputs.demand_shape = shape;

  EvaluationOptions options;
  options.limits = limits_;
  options.now = clock_->wall_now();
  options.epoch = view->epoch;
  options.provenance = view->provenance.source_id.valid() ? view->provenance : provenance_;

  Outcome<CapacitySnapshot> snapshot = evaluate(inputs, options);
  if (!snapshot) {
    rejected_evaluations_.fetch_add(1, std::memory_order_relaxed);
    return snapshot.error();
  }
  publish_snapshot(*snapshot);
  completed_evaluations_.fetch_add(1, std::memory_order_relaxed);
  return snapshot;
}

Outcome<FitQueryResult> Fabric::fit(const PolicyId& policy, const FitQuery& query) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  const RegistryViewPtr view = registry_.view();
  CFN_RETURN_IF_ERROR(require_inputs(*view));
  const Outcome<CapacityPolicy> selected = policy_for(*view, policy);
  if (!selected) {
    return selected.error();
  }
  Outcome<std::vector<ResourceAccounting>> rows =
      derive_accounting(view->resources, view->topology, view->reservations, view->degradation,
                        view->domains, *selected, limits_);
  if (!rows) {
    return rows.error();
  }
  CapacityPolicy effective = *selected;
  if (query.resilience != ResilienceMode::None) {
    effective.resilience = query.resilience;
  }
  const Outcome<FitQueryResult> answer =
      fit_query(*rows, view->topology, query, limits_, CancellationToken{});
  if (!answer) {
    rejected_evaluations_.fetch_add(1, std::memory_order_relaxed);
    return answer.error();
  }
  completed_evaluations_.fetch_add(1, std::memory_order_relaxed);
  return answer;
}

Outcome<PredictionResult> Fabric::predict(const PredictionRequest& request) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  PredictionRequest effective = request;
  effective.epoch = epoch();
  if (effective.history.empty()) {
    const RegistryViewPtr view = registry_.view();
    effective.history = view->observations;
  }
  return cfn::predict(effective, limits_, clock_->wall_now());
}

Outcome<void> Fabric::validate_snapshot(const CapacitySnapshot& snapshot) const {
  const RegistryViewPtr view = registry_.view();
  GenerationVector current;
  {
    current.fabric_epoch = view->epoch;
    current.model = snapshot.generations.model;
    current.policy = snapshot.generations.policy;
    current.topology = view->topology.generation;
    current.resource_catalog = view->resources.generation;
    current.reservation_snapshot = view->reservations.generation;
    current.failure_domain_catalog = view->domains.generation;
    current.degradation = view->degradation.generation;
    current.demand_shape = snapshot.generations.demand_shape;
    current.resource_set = resource_set_digest(view->resources);
    current.resource_count = static_cast<std::uint32_t>(view->resources.resources.size());
    current.computed_at = clock_->wall_now();
    current.provenance = view->provenance;
  }
  return validate_binding(snapshot, current);
}

void Fabric::publish_snapshot(const CapacitySnapshot& snapshot) {
  SnapshotRecord record;
  record.id = snapshot.id;
  record.generation = snapshot.generation;
  record.model = snapshot.model;
  record.generations = snapshot.generations;
  record.rollup = snapshot.rollup;
  record.recorded_at = snapshot.generations.computed_at;
  {
    const std::lock_guard<std::mutex> guard(history_mutex_);
    history_.push_back(record);
    while (history_.size() > limits_.max_history_entries) {
      history_.erase(history_.begin());
    }
  }
  if (store_ != nullptr) {
    if (!store_->append_history(record)) {
      history_append_failures_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

Outcome<void> Fabric::record_observation(CapacityObservation observation) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return Error(ErrorCode::Closed, "the fabric is shutting down");
  }
  return registry_.record_observation(std::move(observation));
}

Outcome<std::vector<SnapshotRecord>> Fabric::history() const {
  const std::lock_guard<std::mutex> guard(history_mutex_);
  return history_;
}

Outcome<std::optional<CapacityModel>> Fabric::find_model(const CapacityModelId& id) const {
  const RegistryViewPtr view = registry_.view();
  const auto found = view->models.find(id);
  if (found == view->models.end()) {
    return std::optional<CapacityModel>{};
  }
  return std::optional<CapacityModel>{found->second};
}

Outcome<std::optional<CapacityPolicy>> Fabric::find_policy(const PolicyId& id) const {
  const RegistryViewPtr view = registry_.view();
  const auto found = view->policies.find(id);
  if (found == view->policies.end()) {
    return std::optional<CapacityPolicy>{};
  }
  return std::optional<CapacityPolicy>{found->second};
}

Outcome<void> Fabric::compact() {
  if (store_ == nullptr) {
    return Error(ErrorCode::InvalidState, "the fabric has no durable store to compact");
  }
  return store_->compact();
}

Outcome<void> Fabric::shutdown() {
  if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
    return Outcome<void>();
  }
  {
    const std::lock_guard<std::mutex> guard(lifecycle_mutex_);
    for (CancellationToken& token : active_tokens_) {
      token.cancel();
    }
  }
  // Wait for in-flight evaluations. No lock is held while waiting, and the
  // evaluation path checks the token at bounded intervals, so this terminates.
  {
    std::unique_lock<std::mutex> guard(lifecycle_mutex_);
    while (active_evaluations_.load(std::memory_order_acquire) != 0) {
      lifecycle_cv_.wait_for(guard, std::chrono::milliseconds(1));
    }
  }
  Outcome<void> result;
  if (store_ != nullptr) {
    result = store_->close();
    store_.reset();
  }
  return result;
}

}  // namespace cfn