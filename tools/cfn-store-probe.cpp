// Capacity Fabric Network - durable store probe.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A real OS process used by the durability and multiprocess tests. It performs
// one durable operation and then either closes cleanly or dies without closing,
// which is exactly the distinction recovery has to detect.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

/// Terminates the process immediately: no destructors, no at-exit handlers, and
/// no Windows fault reporting dialog. This is the "killed writer" the durability
/// tests need; the point is that the store never gets to write its
/// clean-shutdown marker.
[[noreturn]] void die_without_closing() {
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(3);
}

}  // namespace

#include "cfn/cfn.hpp"

namespace {

[[nodiscard]] cfn::CapacityModel make_model(const std::string& name, std::uint64_t ordinal) {
  cfn::CapacityModel model;
  const auto id = cfn::CapacityModelId::parse(name + "-" + std::to_string(ordinal));
  model.id = id.has_value() ? *id : cfn::CapacityModelId{};
  model.generation = cfn::Generation::initial();
  const auto policy = cfn::PolicyId::parse("probe-policy");
  model.policy = policy.has_value() ? *policy : cfn::PolicyId{};
  model.policy_generation = cfn::Generation::initial();
  const auto source = cfn::EvidenceSourceId::parse("store-probe");
  if (source.has_value()) {
    model.provenance = cfn::derived_provenance(*source, cfn::Timestamp{});
  }
  return model;
}

[[nodiscard]] cfn::CapacityPolicy make_policy() {
  cfn::CapacityPolicy policy;
  const auto id = cfn::PolicyId::parse("probe-policy");
  policy.id = id.has_value() ? *id : cfn::PolicyId{};
  policy.generation = cfn::Generation::initial();
  const auto source = cfn::EvidenceSourceId::parse("store-probe");
  if (source.has_value()) {
    policy.provenance = cfn::declared_provenance(*source, cfn::Timestamp{}, cfn::Duration{});
  }
  return policy;
}

[[nodiscard]] cfn::SnapshotRecord make_history(std::uint64_t ordinal) {
  cfn::SnapshotRecord record;
  const auto id = cfn::CapacitySnapshotId::parse("probe-snapshot-" + std::to_string(ordinal));
  record.id = id.has_value() ? *id : cfn::CapacitySnapshotId{};
  record.generation = cfn::Generation::from_value(ordinal + 1);
  const auto model = cfn::CapacityModelId::parse("probe-model-0");
  record.model = model.has_value() ? *model : cfn::CapacityModelId{};
  record.generations.fabric_epoch = cfn::FabricEpoch::initial();
  record.generations.model = cfn::Generation::initial();
  record.generations.policy = cfn::Generation::initial();
  record.generations.topology = cfn::Generation::initial();
  record.generations.resource_catalog = cfn::Generation::initial();
  record.generations.reservation_snapshot = cfn::Generation::initial();
  record.generations.failure_domain_catalog = cfn::Generation::initial();
  record.generations.degradation = cfn::Generation::initial();
  record.generations.demand_shape = cfn::Generation::initial();
  record.rollup.raw_total = cfn::Capacity::from_units(1000 + ordinal);
  record.rollup.available_total = cfn::Capacity::from_units(1000 + ordinal);
  record.rollup.usable_total = cfn::Capacity::from_units(1000 + ordinal);
  record.recorded_at = cfn::Timestamp::from_unix_seconds(static_cast<std::int64_t>(ordinal));
  return record;
}

[[nodiscard]] bool open_store(const std::string& directory, std::unique_ptr<cfn::CapacityStore>& store,
                              cfn::RecoveryReport& report) {
  cfn::StoreOptions options;
  options.directory = directory;
  options.durability_barrier = true;
  cfn::SystemClock clock;
  cfn::Outcome<std::unique_ptr<cfn::CapacityStore>> opened =
      cfn::CapacityStore::open(options, clock, report);
  if (!opened) {
    std::printf("error=%s\n", opened.error().to_text().c_str());
    return false;
  }
  store = std::move(*opened);
  return true;
}

void print_report(const cfn::RecoveryReport& report) {
  std::printf(
      "state_present=%d state_accepted=%d state_rejected=%d journal_records=%llu applied=%llu "
      "rejected=%llu unfinished=%llu corrupt=%llu truncated_tail=%d previous_epoch=%llu "
      "current_epoch=%llu previous_boot=%llu current_boot=%llu epoch_advanced=%d fenced=%u "
      "revalidation=%u models=%u policies=%u history=%u\n",
      report.state_present ? 1 : 0, report.state_accepted ? 1 : 0, report.state_rejected ? 1 : 0,
      static_cast<unsigned long long>(report.journal_records_scanned),
      static_cast<unsigned long long>(report.journal_records_applied),
      static_cast<unsigned long long>(report.journal_records_rejected),
      static_cast<unsigned long long>(report.unfinished_attempts),
      static_cast<unsigned long long>(report.corrupt_records), report.journal_truncated_tail ? 1 : 0,
      static_cast<unsigned long long>(report.previous_epoch.value()),
      static_cast<unsigned long long>(report.current_epoch.value()),
      static_cast<unsigned long long>(report.previous_boot.value()),
      static_cast<unsigned long long>(report.current_boot.value()), report.epoch_advanced ? 1 : 0,
      report.live_authority_fenced, report.evidence_requiring_revalidation, report.models_loaded,
      report.policies_loaded, report.history_loaded);
}

int usage() {
  std::printf(
      "usage:\n"
      "  cfn-store-probe open <dir>\n"
      "  cfn-store-probe write <dir> <count> [clean|crash]\n"
      "  cfn-store-probe grant <dir> <holder> [clean|crash]\n"
      "  cfn-store-probe check <dir> <holder>\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    return usage();
  }
  const std::string command = argv[1];
  const std::string directory = argv[2];

  if (command == "open") {
    std::unique_ptr<cfn::CapacityStore> store;
    cfn::RecoveryReport report;
    if (!open_store(directory, store, report)) {
      return 1;
    }
    print_report(report);
    if (!store->close()) {
      return 1;
    }
    return 0;
  }

  if (command == "write") {
    const std::uint64_t count = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
    const std::string mode = argc > 4 ? argv[4] : "clean";
    std::unique_ptr<cfn::CapacityStore> store;
    cfn::RecoveryReport report;
    if (!open_store(directory, store, report)) {
      return 1;
    }
    if (!store->put_policy(make_policy())) {
      return 1;
    }
    for (std::uint64_t index = 0; index < count; ++index) {
      if (!store->put_model(make_model("probe-model", index))) {
        return 1;
      }
      if (!store->append_history(make_history(index))) {
        return 1;
      }
    }
    std::printf("wrote=%llu\n", static_cast<unsigned long long>(count));
    std::fflush(stdout);
    if (mode == "crash") {
      die_without_closing();
    }
    if (!store->close()) {
      return 1;
    }
    return 0;
  }

  if (command == "grant") {
    if (argc < 4) {
      return usage();
    }
    const std::string holder = argv[3];
    const std::string mode = argc > 4 ? argv[4] : "clean";
    std::unique_ptr<cfn::CapacityStore> store;
    cfn::RecoveryReport report;
    if (!open_store(directory, store, report)) {
      return 1;
    }
    cfn::LiveAuthorityRecord record;
    record.kind = cfn::LiveAuthorityKind::Publisher;
    const auto id = cfn::GenericId::parse(holder);
    record.holder = id.has_value() ? *id : cfn::GenericId{};
    record.epoch = store->epoch();
    record.boot = store->boot();
    record.granted_at = cfn::Timestamp::from_unix_seconds(1000);
    record.ttl = cfn::Duration::from_hours(1);
    record.sequence = cfn::Sequence::initial();
    if (!store->grant_live_authority(record)) {
      return 1;
    }
    std::printf("granted=%s epoch=%llu boot=%llu\n", holder.c_str(),
                static_cast<unsigned long long>(store->epoch().value()),
                static_cast<unsigned long long>(store->boot().value()));
    std::fflush(stdout);
    if (mode == "crash") {
      die_without_closing();
    }
    if (!store->close()) {
      return 1;
    }
    return 0;
  }

  if (command == "check") {
    if (argc < 4) {
      return usage();
    }
    const std::string holder = argv[3];
    std::unique_ptr<cfn::CapacityStore> store;
    cfn::RecoveryReport report;
    if (!open_store(directory, store, report)) {
      return 1;
    }
    const auto id = cfn::GenericId::parse(holder);
    const cfn::GenericId key = id.has_value() ? *id : cfn::GenericId{};
    const bool valid_now = store->live_authority_valid(key, cfn::Timestamp::from_unix_seconds(1000));
    const std::map<cfn::GenericId, cfn::LiveAuthorityRecord> authority = store->live_authority();
    const bool persisted = authority.find(key) != authority.end();
    std::printf("holder=%s persisted=%d valid=%d fenced=%u\n", holder.c_str(), persisted ? 1 : 0,
                valid_now ? 1 : 0, report.live_authority_fenced);
    if (!store->close()) {
      return 1;
    }
    return 0;
  }

  return usage();
}