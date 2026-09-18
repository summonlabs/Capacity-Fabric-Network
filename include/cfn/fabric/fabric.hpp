// Capacity Fabric Network - facade.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The fabric binds the authoritative registries, the durable store, the clock,
// and the evaluator. Every mutating call follows the same order:
//
//   validate -> bind authority -> plan -> journal -> apply -> publish
//
// A durable mutation is never acknowledged before the record is on stable
// storage. Shutdown stops accepting work, cancels work in flight, and only
// then releases resources.
#ifndef CFN_FABRIC_FABRIC_HPP
#define CFN_FABRIC_FABRIC_HPP

#include <atomic>
#include <condition_variable>
#include <memory>
#include <string>

#include "cfn/core/cancel.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/time.hpp"
#include "cfn/engine/evaluator.hpp"
#include "cfn/engine/invalidation.hpp"
#include "cfn/engine/prediction.hpp"
#include "cfn/fabric/registry.hpp"
#include "cfn/persist/store.hpp"

namespace cfn {

struct FabricOptions {
  Limits limits;
  std::shared_ptr<Clock> clock;
  /// Durable store directory. An empty path keeps the fabric in memory only;
  /// configuration is then not durable and the recovery report says so.
  std::filesystem::path store_directory;
  bool allow_state_discard_on_corruption = false;
  /// Provenance stamped on locally produced state.
  Provenance provenance;
  /// When true the fabric refuses to evaluate while any authoritative input
  /// has expired evidence.
  bool require_fresh_evidence = false;
};

class CFN_API Fabric {
 public:
  static Outcome<std::unique_ptr<Fabric>> open(const FabricOptions& options);

  ~Fabric();
  Fabric(const Fabric&) = delete;
  Fabric& operator=(const Fabric&) = delete;

  [[nodiscard]] FabricEpoch epoch() const;
  [[nodiscard]] const Limits& limits() const noexcept { return limits_; }
  [[nodiscard]] const RecoveryReport& recovery() const noexcept;
  [[nodiscard]] bool durable() const noexcept;
  [[nodiscard]] RegistryViewPtr view() const { return registry_.view(); }

  // --- authoritative inputs -------------------------------------------------
  [[nodiscard]] Outcome<void> set_resource_catalog(ResourceCatalog catalog);
  [[nodiscard]] Outcome<void> upsert_resource(ResourceRecord record);
  [[nodiscard]] Outcome<void> remove_resource(const ResourceId& id);
  [[nodiscard]] Outcome<void> set_topology(Topology topology);
  [[nodiscard]] Outcome<void> set_reservations(ReservationSnapshot snapshot);
  [[nodiscard]] Outcome<void> set_failure_domains(FailureDomainCatalog catalog);
  [[nodiscard]] Outcome<void> set_degradation(DegradationSnapshot snapshot);

  // --- owned configuration --------------------------------------------------
  [[nodiscard]] Outcome<void> register_policy(CapacityPolicy policy);
  [[nodiscard]] Outcome<void> register_demand_shape(DemandShape shape);
  [[nodiscard]] Outcome<void> register_model(CapacityModel model);
  [[nodiscard]] Outcome<void> remove_model(const CapacityModelId& id);

  // --- evaluation -----------------------------------------------------------
  [[nodiscard]] Outcome<CapacitySnapshot> compute(const CapacityModelId& model);
  [[nodiscard]] Outcome<CapacitySnapshot> compute_ad_hoc(const CapacityPolicy& policy, const DemandShape* shape);
  [[nodiscard]] Outcome<FitQueryResult> fit(const PolicyId& policy, const FitQuery& query);
  [[nodiscard]] Outcome<PredictionResult> predict(const PredictionRequest& request);
  [[nodiscard]] Outcome<void> validate_snapshot(const CapacitySnapshot& snapshot) const;

  // --- history --------------------------------------------------------------
  [[nodiscard]] Outcome<void> record_observation(CapacityObservation observation);
  [[nodiscard]] Outcome<std::vector<SnapshotRecord>> history() const;
  [[nodiscard]] Outcome<std::optional<CapacityModel>> find_model(const CapacityModelId& id) const;
  [[nodiscard]] Outcome<std::optional<CapacityPolicy>> find_policy(const PolicyId& id) const;

  // --- durability -----------------------------------------------------------
  [[nodiscard]] Outcome<void> compact();

  /// Stops accepting work, cancels work in flight, waits for it to finish, and
  /// releases the store. Returns an error only when cleanup itself failed.
  [[nodiscard]] Outcome<void> shutdown();
  [[nodiscard]] bool shutting_down() const noexcept { return shutting_down_.load(std::memory_order_acquire); }

  /// Monotonic count of evaluations that completed successfully.
  [[nodiscard]] std::uint64_t completed_evaluations() const noexcept {
    return completed_evaluations_.load(std::memory_order_relaxed);
  }
  /// Monotonic count of evaluations rejected or cancelled.
  [[nodiscard]] std::uint64_t rejected_evaluations() const noexcept {
    return rejected_evaluations_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t active_evaluations() const noexcept {
    return active_evaluations_.load(std::memory_order_relaxed);
  }
  /// Number of history records that could not be written to the durable store.
  [[nodiscard]] std::uint64_t history_append_failures() const noexcept {
    return history_append_failures_.load(std::memory_order_relaxed);
  }

 private:
  Fabric(const FabricOptions& options, FabricEpoch epoch, RegistryViewPtr initial, RecoveryReport report);

  [[nodiscard]] Outcome<CapacitySnapshot> evaluate_view(const RegistryView& view, const CapacityModel& model,
                                                        Timestamp now) const;
  void publish_snapshot(const CapacitySnapshot& snapshot);

  Limits limits_;
  std::shared_ptr<Clock> clock_;
  Provenance provenance_;
  Registry registry_;
  std::unique_ptr<CapacityStore> store_;
  RecoveryReport recovery_;
  bool durable_ = false;
  bool require_fresh_evidence_ = false;

  mutable std::mutex history_mutex_;
  std::vector<SnapshotRecord> history_;

  std::atomic<bool> shutting_down_{false};
  std::atomic<std::uint64_t> completed_evaluations_{0};
  std::atomic<std::uint64_t> rejected_evaluations_{0};
  std::atomic<std::uint64_t> active_evaluations_{0};
  std::atomic<std::uint64_t> history_append_failures_{0};

  mutable std::mutex lifecycle_mutex_;
  std::condition_variable lifecycle_cv_;
  std::vector<CancellationToken> active_tokens_;
  bool store_released_ = false;
};

}  // namespace cfn

#endif  // CFN_FABRIC_FABRIC_HPP