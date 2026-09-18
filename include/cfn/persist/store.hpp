// Capacity Fabric Network - crash safe durable store.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Directory layout:
//   state.bin    compacted authoritative configuration and history, CRC framed
//   state.bin.tmp  temporary during compaction; never read
//   journal.log  append-only record stream since the last checkpoint
//
// Write order for every durable mutation is
//   validate -> bind authority -> plan -> journal -> fsync -> apply -> publish
// and for compaction
//   write temp -> fsync temp -> rename -> fsync directory -> reset journal.
//
// Recovery classifies what it finds: durable configuration, committed
// authoritative state, unfinished attempts, ambiguous outcomes, stale live
// authority, and evidence that requires revalidation. Live authority is never
// restored: it is reported as fenced.
#ifndef CFN_PERSIST_STORE_HPP
#define CFN_PERSIST_STORE_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/time.hpp"
#include "cfn/persist/journal.hpp"
#include "cfn/persist/records.hpp"
#include "cfn/persist/serialize.hpp"

namespace cfn {

struct StoreOptions {
  std::filesystem::path directory;
  Limits limits;
  /// When false, a corrupt state file fails the open instead of being
  /// discarded. Discarding is always reported in the recovery report.
  bool allow_state_discard_on_corruption = false;
  /// When false, writes skip the durability barrier. Only benchmark and test
  /// harnesses ever set this; the default is true.
  bool durability_barrier = true;
};

struct RecoveryReport {
  bool opened = false;
  bool durable = false;
  bool state_present = false;
  bool state_accepted = false;
  bool state_rejected = false;
  std::string state_rejection_detail;

  std::uint64_t journal_records_scanned = 0;
  std::uint64_t journal_records_applied = 0;
  std::uint64_t journal_records_rejected = 0;
  std::uint64_t unfinished_attempts = 0;
  std::uint64_t corrupt_records = 0;
  std::uint64_t transactions_applied = 0;
  bool journal_truncated_tail = false;
  bool journal_version_mismatch = false;
  bool journal_header_invalid = false;

  FabricEpoch previous_epoch;
  FabricEpoch current_epoch;
  BootIncarnation previous_boot;
  BootIncarnation current_boot;
  bool epoch_advanced = false;

  std::uint32_t policies_loaded = 0;
  std::uint32_t demand_shapes_loaded = 0;
  std::uint32_t models_loaded = 0;
  std::uint32_t history_loaded = 0;
  std::uint32_t observations_loaded = 0;
  /// Live authority records found on disk and deliberately not restored.
  std::uint32_t live_authority_fenced = 0;
  /// External evidence references that must be re-observed before use.
  std::uint32_t evidence_requiring_revalidation = 0;
  bool requires_revalidation = false;

  std::vector<std::string> diagnostics;

  [[nodiscard]] friend bool operator==(const RecoveryReport&, const RecoveryReport&) noexcept = default;
};

class CFN_API CapacityStore {
 public:
  static Outcome<std::unique_ptr<CapacityStore>> open(const StoreOptions& options, Clock& clock,
                                                      RecoveryReport& report_out);

  ~CapacityStore();
  CapacityStore(const CapacityStore&) = delete;
  CapacityStore& operator=(const CapacityStore&) = delete;

  [[nodiscard]] FabricEpoch epoch() const noexcept { return epoch_; }
  [[nodiscard]] BootIncarnation boot() const noexcept { return boot_; }
  [[nodiscard]] const RecoveryReport& recovery() const noexcept { return report_; }
  [[nodiscard]] bool durable() const noexcept { return options_.durability_barrier; }

  // --- durable mutations ----------------------------------------------------
  [[nodiscard]] Outcome<void> put_policy(const CapacityPolicy& policy);
  [[nodiscard]] Outcome<void> remove_policy(const PolicyId& id);
  [[nodiscard]] Outcome<void> put_demand_shape(const DemandShape& shape);
  [[nodiscard]] Outcome<void> remove_demand_shape(const DemandShapeId& id);
  [[nodiscard]] Outcome<void> put_model(const CapacityModel& model);
  [[nodiscard]] Outcome<void> remove_model(const CapacityModelId& id);
  [[nodiscard]] Outcome<void> append_history(const SnapshotRecord& record);
  [[nodiscard]] Outcome<void> put_evidence_reference(const EvidenceReference& reference);
  [[nodiscard]] Outcome<void> grant_live_authority(const LiveAuthorityRecord& record);
  [[nodiscard]] Outcome<void> advance_epoch();

  // --- reads ----------------------------------------------------------------
  /// Reads return a copy taken under the store lock, so a caller never holds a
  /// reference into state that a concurrent mutation could be rewriting.
  [[nodiscard]] std::map<PolicyId, CapacityPolicy> policies() const;
  [[nodiscard]] std::map<DemandShapeId, DemandShape> demand_shapes() const;
  [[nodiscard]] std::map<CapacityModelId, CapacityModel> models() const;
  [[nodiscard]] std::vector<SnapshotRecord> history() const;
  [[nodiscard]] std::map<EvidenceSourceId, EvidenceReference> evidence_references() const;
  [[nodiscard]] std::map<GenericId, LiveAuthorityRecord> live_authority() const;

  /// A persisted authority claim is valid only inside the boot incarnation and
  /// fabric epoch that granted it, and only before its TTL expires.
  [[nodiscard]] bool live_authority_valid(const GenericId& holder, Timestamp now) const;

  /// Writes the compacted state and resets the journal. Crash safe.
  [[nodiscard]] Outcome<void> compact();
  [[nodiscard]] Outcome<void> close();

  [[nodiscard]] const std::filesystem::path& directory() const noexcept { return options_.directory; }

 private:
  CapacityStore(StoreOptions options, Clock& clock);

  [[nodiscard]] Outcome<void> load_state(bool& rejected_corrupt);
  [[nodiscard]] Outcome<void> replay_journal();

  StoreOptions options_;
  Clock* clock_ = nullptr;
  /// Guards every durable mutation and every read of the in-memory state.
  mutable std::mutex mutex_;
  FabricEpoch epoch_;
  BootIncarnation boot_;
  StoreState state_;
  RecoveryReport report_;
  std::unique_ptr<Journal> journal_;
  bool closed_ = false;

};

/// Encodes and decodes the compacted state file payload.
[[nodiscard]] CFN_API Outcome<std::vector<std::byte>> encode_store_state(const StoreState& state,
                                                                         const Limits& limits);
[[nodiscard]] CFN_API Outcome<StoreState> decode_store_state(std::span<const std::byte> payload,
                                                             const Limits& limits);

}  // namespace cfn

#endif  // CFN_PERSIST_STORE_HPP