// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

[[nodiscard]] CapacityPolicy probe_policy() {
  CapacityPolicy policy;
  const auto id = PolicyId::parse("stored-policy");
  policy.id = id.has_value() ? *id : PolicyId{};
  policy.generation = Generation::initial();
  policy.provenance = declared_provenance(cfn::test::test_source(), cfn::test::test_now(), Duration{});
  return policy;
}

[[nodiscard]] CapacityModel probe_model(const char* name) {
  CapacityModel model;
  const auto id = CapacityModelId::parse(name);
  model.id = id.has_value() ? *id : CapacityModelId{};
  model.generation = Generation::initial();
  model.policy = probe_policy().id;
  model.policy_generation = Generation::initial();
  model.provenance = derived_provenance(cfn::test::test_source(), cfn::test::test_now());
  return model;
}

[[nodiscard]] SnapshotRecord probe_history(std::uint64_t ordinal) {
  SnapshotRecord record;
  const auto id = CapacitySnapshotId::parse("history-" + std::to_string(ordinal));
  record.id = id.has_value() ? *id : CapacitySnapshotId{};
  record.generation = Generation::from_value(ordinal + 1);
  record.model = probe_model("stored-model").id;
  record.generations.fabric_epoch = FabricEpoch::initial();
  record.rollup.raw_total = Capacity::from_units(100 + ordinal);
  record.recorded_at = cfn::test::test_now();
  return record;
}

class OpenStore {
 public:
  OpenStore(const std::filesystem::path& directory, bool discard = false) : directory_(directory) {
    options_.directory = directory;
    options_.durability_barrier = true;
    options_.allow_state_discard_on_corruption = discard;
  }

  [[nodiscard]] Outcome<std::unique_ptr<CapacityStore>> open() {
    return CapacityStore::open(options_, clock_, report_);
  }

  [[nodiscard]] const RecoveryReport& report() const noexcept { return report_; }

 private:
  std::filesystem::path directory_;
  StoreOptions options_;
  SystemClock clock_;
  RecoveryReport report_;
};

}  // namespace

CFN_TEST(persistence, journal_round_trip) {
  cfn::test::TempDirectory directory("cfn-journal");
  const std::filesystem::path path = directory.path() / "journal.log";
  Limits limits;
  {
    JournalScanResult scan;
    auto opened = Journal::open(path, limits, &scan);
    CFN_REQUIRE_OK(cfn_ctx, opened);
    std::vector<JournalRecord> records;
    JournalRecord first;
    first.kind = JournalRecordKind::PolicyPut;
    first.payload = {std::byte{1}, std::byte{2}, std::byte{3}};
    records.push_back(first);
    JournalRecord second;
    second.kind = JournalRecordKind::ModelPut;
    second.payload = {std::byte{9}};
    records.push_back(second);
    CFN_REQUIRE_OK(cfn_ctx, opened->append_transaction(records));
    CFN_REQUIRE_OK(cfn_ctx, opened->sync());
    opened->close();
  }
  const auto scan = scan_journal(path, limits);
  CFN_REQUIRE_OK(cfn_ctx, scan);
  CFN_CHECK_EQ(scan->records.size(), 2U);
  CFN_CHECK_EQ(scan->transactions_applied, 1U);
  CFN_CHECK_EQ(scan->unfinished_attempts, 0U);
  CFN_CHECK(!scan->truncated_tail);
}

CFN_TEST(persistence, unfinished_transaction_is_not_applied) {
  cfn::test::TempDirectory directory("cfn-journal-partial");
  const std::filesystem::path path = directory.path() / "journal.log";
  Limits limits;
  {
    JournalScanResult scan;
    auto opened = Journal::open(path, limits, &scan);
    CFN_REQUIRE_OK(cfn_ctx, opened);
    std::vector<JournalRecord> records;
    JournalRecord first;
    first.kind = JournalRecordKind::PolicyPut;
    first.payload = {std::byte{7}};
    records.push_back(first);
    CFN_REQUIRE_OK(cfn_ctx, opened->append_transaction(records));
    CFN_REQUIRE_OK(cfn_ctx, opened->sync());
    // Begin plus data without a commit, exactly what a killed writer leaves.
    const std::vector<std::byte> payload(12, std::byte{0});
    CFN_REQUIRE_OK(cfn_ctx, opened->append(JournalRecordKind::Begin, payload));
    JournalRecord orphan;
    orphan.kind = JournalRecordKind::ModelPut;
    orphan.payload = {std::byte{1}, std::byte{1}};
    CFN_REQUIRE_OK(cfn_ctx, opened->append(orphan.kind, orphan.payload));
    CFN_REQUIRE_OK(cfn_ctx, opened->sync());
    opened->close();
  }
  const auto scan = scan_journal(path, limits);
  CFN_REQUIRE_OK(cfn_ctx, scan);
  CFN_CHECK_EQ(scan->records.size(), 1U);
  CFN_CHECK_EQ(scan->unfinished_attempts, 1U);
  CFN_CHECK_EQ(scan->records_rejected, 1U);
}

CFN_TEST(persistence, torn_tail_is_repaired_on_reopen) {
  cfn::test::TempDirectory directory("cfn-journal-torn");
  const std::filesystem::path path = directory.path() / "journal.log";
  Limits limits;
  std::uint64_t valid_bytes = 0;
  {
    JournalScanResult scan;
    auto opened = Journal::open(path, limits, &scan);
    CFN_REQUIRE_OK(cfn_ctx, opened);
    std::vector<JournalRecord> records;
    JournalRecord first;
    first.kind = JournalRecordKind::PolicyPut;
    first.payload = {std::byte{5}};
    records.push_back(first);
    CFN_REQUIRE_OK(cfn_ctx, opened->append_transaction(records));
    CFN_REQUIRE_OK(cfn_ctx, opened->sync());
    valid_bytes = opened->bytes_written();
    opened->close();
  }
  {
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    const char garbage[] = "\x01\x02\x03\x04\x05\x06\x07\x08garbage-tail";
    stream.write(garbage, static_cast<std::streamsize>(sizeof(garbage) - 1));
  }
  const auto scan = scan_journal(path, limits);
  CFN_REQUIRE_OK(cfn_ctx, scan);
  CFN_CHECK(scan->truncated_tail);
  CFN_CHECK_EQ(scan->records.size(), 1U);
  CFN_CHECK_EQ(scan->valid_bytes, valid_bytes);
  {
    JournalScanResult reopened;
    auto opened = Journal::open(path, limits, &reopened);
    CFN_REQUIRE_OK(cfn_ctx, opened);
    opened->close();
  }
  const auto after = scan_journal(path, limits);
  CFN_REQUIRE_OK(cfn_ctx, after);
  CFN_CHECK(!after->truncated_tail);
  CFN_CHECK_EQ(after->records.size(), 1U);
}

CFN_TEST(persistence, corrupt_payload_checksum_is_detected) {
  cfn::test::TempDirectory directory("cfn-journal-corrupt");
  const std::filesystem::path path = directory.path() / "journal.log";
  Limits limits;
  {
    JournalScanResult scan;
    auto opened = Journal::open(path, limits, &scan);
    CFN_REQUIRE_OK(cfn_ctx, opened);
    std::vector<JournalRecord> records;
    JournalRecord first;
    first.kind = JournalRecordKind::PolicyPut;
    first.payload = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    records.push_back(first);
    CFN_REQUIRE_OK(cfn_ctx, opened->append_transaction(records));
    CFN_REQUIRE_OK(cfn_ctx, opened->sync());
    opened->close();
  }
  {
    std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
    stream.seekp(40);
    const char byte = '\x7F';
    stream.write(&byte, 1);
  }
  const auto scan = scan_journal(path, limits);
  CFN_REQUIRE_OK(cfn_ctx, scan);
  CFN_CHECK(scan->truncated_tail || scan->corrupt_records != 0U);
  CFN_CHECK(scan->records.empty());
}

CFN_TEST(persistence, store_round_trip_preserves_configuration) {
  cfn::test::TempDirectory directory("cfn-store");
  OpenStore opener(directory.path());
  {
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_CHECK_EQ((*store)->epoch().value(), 1ULL);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_policy(probe_policy()));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_model(probe_model("stored-model")));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->append_history(probe_history(0)));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_CHECK_EQ((*store)->policies().size(), 1U);
    CFN_CHECK_EQ((*store)->models().size(), 1U);
    CFN_CHECK_EQ((*store)->history().size(), 1U);
    CFN_CHECK(!opener.report().epoch_advanced);
    CFN_CHECK_EQ(opener.report().previous_epoch.value(), 1ULL);
    CFN_CHECK_EQ(opener.report().current_epoch.value(), 1ULL);
    CFN_CHECK_EQ(opener.report().previous_boot.value(), 1ULL);
    CFN_CHECK_EQ(opener.report().current_boot.value(), 2ULL);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
}

CFN_TEST(persistence, compaction_preserves_state_and_resets_the_journal) {
  cfn::test::TempDirectory directory("cfn-compact");
  OpenStore opener(directory.path());
  {
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_policy(probe_policy()));
    for (int index = 0; index < 16; ++index) {
      CFN_REQUIRE_OK(cfn_ctx, (*store)->put_model(probe_model(("model-" + std::to_string(index)).c_str())));
    }
    CFN_REQUIRE_OK(cfn_ctx, (*store)->compact());
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_model(probe_model("model-after-compaction")));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_CHECK_EQ((*store)->models().size(), 17U);
    CFN_CHECK(opener.report().state_accepted);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
}

CFN_TEST(persistence, corrupt_state_file_is_refused_without_explicit_consent) {
  cfn::test::TempDirectory directory("cfn-corrupt-state");
  {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_policy(probe_policy()));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->compact());
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    std::fstream stream(directory.path() / "state.bin", std::ios::binary | std::ios::in | std::ios::out);
    stream.seekp(30);
    const char byte = '\x5A';
    stream.write(&byte, 1);
  }
  OpenStore strict(directory.path());
  CFN_REQUIRE_ERROR(cfn_ctx, strict.open(), ErrorCode::ChecksumMismatch);
  CFN_CHECK(strict.report().state_rejected);
  CFN_CHECK(!strict.report().state_rejection_detail.empty());

  OpenStore lenient(directory.path(), true);
  const auto store = lenient.open();
  CFN_REQUIRE_OK(cfn_ctx, store);
  CFN_CHECK(lenient.report().state_rejected);
  CFN_CHECK(lenient.report().requires_revalidation);
  CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
}

CFN_TEST(persistence, truncated_state_file_is_refused) {
  cfn::test::TempDirectory directory("cfn-truncated-state");
  {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_policy(probe_policy()));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->compact());
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    const std::filesystem::path state = directory.path() / "state.bin";
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(state, error);
    CFN_CHECK(!error);
    std::filesystem::resize_file(state, size - 4);
  }
  OpenStore strict(directory.path());
  const auto store = strict.open();
  CFN_CHECK(!store);
  if (!store) {
    CFN_CHECK(store.error().code() == ErrorCode::Truncated ||
              store.error().code() == ErrorCode::ChecksumMismatch);
  }
}

CFN_TEST(persistence, live_authority_is_fenced_across_a_restart) {
  cfn::test::TempDirectory directory("cfn-authority");
  const auto holder = GenericId::parse("publisher-1");
  CFN_CHECK(holder.has_value());
  {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    LiveAuthorityRecord record;
    record.kind = LiveAuthorityKind::Publisher;
    record.holder = *holder;
    record.epoch = (*store)->epoch();
    record.boot = (*store)->boot();
    record.granted_at = cfn::test::test_now();
    record.ttl = Duration::from_hours(1);
    record.sequence = Sequence::initial();
    CFN_REQUIRE_OK(cfn_ctx, (*store)->grant_live_authority(record));
    CFN_CHECK((*store)->live_authority_valid(*holder, cfn::test::test_now()));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_CHECK_EQ((*store)->live_authority().size(), 1U);
    CFN_CHECK_EQ(opener.report().live_authority_fenced, 1U);
    // The claim is still readable for audit, but it carries no authority.
    CFN_CHECK(!(*store)->live_authority_valid(*holder, cfn::test::test_now()));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
}

CFN_TEST(persistence, evidence_references_require_revalidation_after_restart) {
  cfn::test::TempDirectory directory("cfn-evidence-ref");
  const auto source = EvidenceSourceId::parse("feed-1");
  CFN_CHECK(source.has_value());
  {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    EvidenceReference reference;
    reference.source = *source;
    reference.generation = Generation::from_value(4);
    reference.last_observed_at = cfn::test::test_now();
    reference.valid_for = Duration::from_hours(1);
    reference.requires_revalidation = false;
    CFN_REQUIRE_OK(cfn_ctx, (*store)->put_evidence_reference(reference));
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    const auto& references = (*store)->evidence_references();
    CFN_CHECK_EQ(references.size(), 1U);
    const auto found = references.find(*source);
    CFN_CHECK(found != references.end());
    if (found != references.end()) {
      CFN_CHECK(found->second.requires_revalidation);
    }
    CFN_CHECK_EQ(opener.report().evidence_requiring_revalidation, 1U);
    CFN_CHECK(opener.report().requires_revalidation);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
}

CFN_TEST(persistence, history_is_bounded) {
  cfn::test::TempDirectory directory("cfn-history");
  Limits limits;
  limits.max_history_entries = 4;
  StoreOptions options;
  options.directory = directory.path();
  options.limits = limits;
  SystemClock clock;
  RecoveryReport report;
  {
    auto store = CapacityStore::open(options, clock, report);
    CFN_REQUIRE_OK(cfn_ctx, store);
    for (int index = 0; index < 10; ++index) {
      CFN_REQUIRE_OK(cfn_ctx, (*store)->append_history(probe_history(static_cast<std::uint64_t>(index))));
    }
    CFN_CHECK_EQ((*store)->history().size(), 4U);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  {
    auto store = CapacityStore::open(options, clock, report);
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_CHECK_EQ((*store)->history().size(), 4U);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
}

CFN_TEST(persistence, writes_after_close_are_refused) {
  cfn::test::TempDirectory directory("cfn-closed");
  OpenStore opener(directory.path());
  auto store = opener.open();
  CFN_REQUIRE_OK(cfn_ctx, store);
  CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  CFN_REQUIRE_ERROR(cfn_ctx, (*store)->put_policy(probe_policy()), ErrorCode::Closed);
  CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
}

CFN_TEST(persistence, a_clean_close_keeps_the_fabric_epoch) {
  cfn::test::TempDirectory directory("cfn-epoch-clean");
  for (int round = 0; round < 3; ++round) {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    if (round == 0) {
      CFN_REQUIRE_OK(cfn_ctx, (*store)->put_policy(probe_policy()));
    }
    // Only the very first open moves the epoch from "not yet established" to
    // its initial value; every clean restart keeps it.
    CFN_CHECK_EQ(opener.report().epoch_advanced, round == 0);
    CFN_CHECK_EQ(opener.report().current_epoch.value(), 1ULL);
    if (round > 0) {
      CFN_CHECK_EQ(opener.report().previous_epoch.value(), 1ULL);
    }
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
}

CFN_TEST(persistence, recovery_reports_the_previous_incarnation) {
  cfn::test::TempDirectory directory("cfn-incarnation");
  {
    OpenStore opener(directory.path());
    const auto store = opener.open();
    CFN_REQUIRE_OK(cfn_ctx, store);
    CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
  }
  OpenStore second(directory.path());
  const auto store = second.open();
  CFN_REQUIRE_OK(cfn_ctx, store);
  CFN_CHECK_EQ(second.report().current_boot.value(),
               second.report().previous_boot.value() + 1ULL);
  CFN_CHECK(second.report().requires_revalidation);
  CFN_REQUIRE_OK(cfn_ctx, (*store)->close());
}

CFN_TEST(persistence, store_rejects_an_empty_directory_option) {
  StoreOptions options;
  SystemClock clock;
  RecoveryReport report;
  CFN_REQUIRE_ERROR(cfn_ctx, CapacityStore::open(options, clock, report), ErrorCode::InvalidArgument);
}