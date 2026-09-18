// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/persist/store.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <system_error>

#include "cfn/core/checked.hpp"
#include "cfn/persist/crc32c.hpp"
#include "cfn/version.hpp"

#if defined(_MSC_VER)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace cfn {

namespace {

constexpr std::uint32_t kStateMagic = 0x534E4643u;
constexpr std::size_t kStateHeaderBytes = 24;
constexpr const char* kStateFileName = "state.bin";
constexpr const char* kStateTempName = "state.bin.tmp";
constexpr const char* kJournalFileName = "journal.log";

void put_u16(std::byte* out, std::uint16_t value) {
  for (std::size_t index = 0; index < 2; ++index) {
    out[index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
  }
}

void put_u32(std::byte* out, std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    out[index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
  }
}

void put_u64(std::byte* out, std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    out[index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
  }
}

[[nodiscard]] std::uint16_t get_u16(const std::byte* in) {
  std::uint16_t value = 0;
  for (std::size_t index = 0; index < 2; ++index) {
    value |= static_cast<std::uint16_t>(static_cast<std::uint8_t>(in[index])) << (8U * index);
  }
  return value;
}

[[nodiscard]] std::uint32_t get_u32(const std::byte* in) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[index])) << (8U * index);
  }
  return value;
}

[[nodiscard]] std::uint64_t get_u64(const std::byte* in) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(in[index])) << (8U * index);
  }
  return value;
}

[[nodiscard]] Outcome<void> sync_file(std::FILE* file) {
  if (std::fflush(file) != 0) {
    return Error(ErrorCode::IoError, "failed to flush a durable file");
  }
#if defined(_MSC_VER)
  if (_commit(_fileno(file)) != 0) {
    return Error(ErrorCode::IoError, "failed to commit a durable file to stable storage");
  }
#else
  if (fsync(fileno(file)) != 0) {
    return Error(ErrorCode::IoError, "failed to commit a durable file to stable storage");
  }
#endif
  return Outcome<void>();
}

[[nodiscard]] Outcome<void> sync_directory(const std::filesystem::path& directory) {
#if defined(_MSC_VER)
  (void)directory;
  return Outcome<void>();
#else
  const int descriptor = ::open(directory.string().c_str(), O_RDONLY);
  if (descriptor < 0) {
    return Error(ErrorCode::IoError, "failed to open the store directory for syncing");
  }
  const int result = fsync(descriptor);
  ::close(descriptor);
  if (result != 0) {
    return Error(ErrorCode::IoError, "failed to sync the store directory");
  }
  return Outcome<void>();
#endif
}

[[nodiscard]] Outcome<std::vector<std::byte>> read_whole_file(const std::filesystem::path& path,
                                                              std::uint64_t max_bytes) {
  std::FILE* file = nullptr;
#if defined(_MSC_VER)
  if (fopen_s(&file, path.string().c_str(), "rb") != 0) {
    file = nullptr;
  }
#else
  file = std::fopen(path.string().c_str(), "rb");
#endif
  if (file == nullptr) {
    return Error(ErrorCode::NotFound, "durable file is not present", path.string());
  }
  std::error_code size_error;
  const std::uintmax_t size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    std::fclose(file);
    return Error(ErrorCode::IoError, "failed to determine the size of a durable file", path.string());
  }
  if (size > max_bytes) {
    std::fclose(file);
    return Error(ErrorCode::LimitExceeded, "durable file exceeds the configured maximum size",
                 path.string());
  }
  std::vector<std::byte> data(static_cast<std::size_t>(size));
  if (!data.empty() && std::fread(data.data(), 1, data.size(), file) != data.size()) {
    std::fclose(file);
    return Error(ErrorCode::IoError, "failed to read a durable file", path.string());
  }
  std::fclose(file);
  return data;
}

[[nodiscard]] Outcome<void> write_whole_file(const std::filesystem::path& path,
                                             std::span<const std::byte> data, bool barrier) {
  std::FILE* file = nullptr;
#if defined(_MSC_VER)
  if (fopen_s(&file, path.string().c_str(), "wb") != 0) {
    file = nullptr;
  }
#else
  file = std::fopen(path.string().c_str(), "wb");
#endif
  if (file == nullptr) {
    return Error(ErrorCode::IoError, "failed to create a durable file", path.string());
  }
  if (!data.empty() && std::fwrite(data.data(), 1, data.size(), file) != data.size()) {
    std::fclose(file);
    return Error(ErrorCode::IoError, "failed to write a durable file", path.string());
  }
  if (barrier) {
    const Outcome<void> synced = sync_file(file);
    std::fclose(file);
    return synced;
  }
  std::fclose(file);
  return Outcome<void>();
}

}  // namespace

std::string_view to_string(LiveAuthorityKind kind) noexcept {
  switch (kind) {
    case LiveAuthorityKind::Publisher: return "publisher";
    case LiveAuthorityKind::Worker: return "worker";
    case LiveAuthorityKind::Lease: return "lease";
    case LiveAuthorityKind::TelemetryFreshness: return "telemetry-freshness";
  }
  return "unknown";
}

bool live_authority_kind_from_string(std::string_view text, LiveAuthorityKind& out) noexcept {
  if (text == "publisher") { out = LiveAuthorityKind::Publisher; return true; }
  if (text == "worker") { out = LiveAuthorityKind::Worker; return true; }
  if (text == "lease") { out = LiveAuthorityKind::Lease; return true; }
  if (text == "telemetry-freshness") { out = LiveAuthorityKind::TelemetryFreshness; return true; }
  return false;
}

Outcome<std::vector<std::byte>> encode_store_state(const StoreState& state, const Limits& limits) {
  serialize::Writer writer;
  writer.u64(state.last_sequence);
  writer.epoch(state.epoch);
  writer.boot(state.boot);
  writer.boolean(state.clean_shutdown);

  // Every count is written before any item so the decoder can validate the
  // whole declared population before allocating anything for it.
  writer.u32(static_cast<std::uint32_t>(state.policies.size()));
  writer.u32(static_cast<std::uint32_t>(state.demand_shapes.size()));
  writer.u32(static_cast<std::uint32_t>(state.models.size()));
  writer.u32(static_cast<std::uint32_t>(state.history.size()));
  writer.u32(static_cast<std::uint32_t>(state.evidence_references.size()));
  writer.u32(static_cast<std::uint32_t>(state.live_authority.size()));
  for (const auto& entry : state.policies) {
    serialize::encode(writer, entry.second);
  }
  for (const auto& entry : state.demand_shapes) {
    serialize::encode(writer, entry.second);
  }
  for (const auto& entry : state.models) {
    serialize::encode(writer, entry.second);
  }
  for (const SnapshotRecord& record : state.history) {
    serialize::encode(writer, record);
  }
  for (const auto& entry : state.evidence_references) {
    serialize::encode(writer, entry.second);
  }
  for (const auto& entry : state.live_authority) {
    serialize::encode(writer, entry.second);
  }
  if (writer.size() > limits.max_journal_bytes) {
    return Error(ErrorCode::LimitExceeded, "compacted state exceeds the configured maximum size");
  }
  return writer.take();
}

Outcome<StoreState> decode_store_state(std::span<const std::byte> payload, const Limits& limits) {
  serialize::Reader reader(payload);
  StoreState state;
  if (!reader.u64(state.last_sequence) || !reader.epoch(state.epoch) || !reader.boot(state.boot) ||
      !reader.boolean(state.clean_shutdown)) {
    return Error(ErrorCode::Truncated, "compacted state header is malformed");
  }
  std::uint32_t policies = 0;
  std::uint32_t shapes = 0;
  std::uint32_t models = 0;
  std::uint32_t history = 0;
  std::uint32_t evidence = 0;
  std::uint32_t authority = 0;
  if (!reader.u32(policies) || !reader.u32(shapes) || !reader.u32(models) ||
      !reader.u32(history) || !reader.u32(evidence) || !reader.u32(authority)) {
    return Error(ErrorCode::Truncated, "compacted state index is malformed");
  }
  if (policies > limits.max_policies || shapes > limits.max_demand_shapes ||
      models > limits.max_models || history > limits.max_history_entries ||
      evidence > limits.max_policies || authority > limits.max_policies) {
    return Error(ErrorCode::LimitExceeded, "compacted state declares an impossible population");
  }
  for (std::uint32_t index = 0; index < policies; ++index) {
    CapacityPolicy value;
    CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
    state.policies[value.id] = value;
  }
  for (std::uint32_t index = 0; index < shapes; ++index) {
    DemandShape value;
    CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
    state.demand_shapes[value.id] = value;
  }
  for (std::uint32_t index = 0; index < models; ++index) {
    CapacityModel value;
    CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
    state.models[value.id] = value;
  }
  for (std::uint32_t index = 0; index < history; ++index) {
    SnapshotRecord value;
    CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
    state.history.push_back(value);
  }
  for (std::uint32_t index = 0; index < evidence; ++index) {
    EvidenceReference value;
    CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
    state.evidence_references[value.source] = value;
  }
  for (std::uint32_t index = 0; index < authority; ++index) {
    LiveAuthorityRecord value;
    CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
    state.live_authority[value.holder] = value;
  }
  if (!reader.at_end()) {
    return Error(ErrorCode::Corrupt, "compacted state carries trailing bytes");
  }
  return state;
}

CapacityStore::CapacityStore(StoreOptions options, Clock& clock)
    : options_(std::move(options)), clock_(&clock), epoch_(FabricEpoch::initial()),
      boot_(BootIncarnation::initial()) {}

CapacityStore::~CapacityStore() { (void)close(); }

Outcome<std::unique_ptr<CapacityStore>> CapacityStore::open(const StoreOptions& options, Clock& clock,
                                                            RecoveryReport& report_out) {
  // The report is published on every exit path, including failures, so a caller
  // always learns what recovery found before the open was rejected.
  std::unique_ptr<CapacityStore> store(new CapacityStore(options, clock));
  struct Publisher {
    RecoveryReport* target;
    const RecoveryReport* source;
    ~Publisher() { *target = *source; }
  } publisher{&report_out, &store->report_};

  CFN_RETURN_IF_ERROR(options.limits.validate());
  if (options.directory.empty()) {
    return Error(ErrorCode::InvalidArgument, "durable store needs a directory");
  }
  store->report_.opened = true;
  store->report_.durable = options.durability_barrier;

  std::error_code directory_error;
  std::filesystem::create_directories(options.directory, directory_error);
  if (directory_error) {
    return Error(ErrorCode::IoError, "failed to create the durable store directory",
                 options.directory.string());
  }

  const std::filesystem::path state_path = options.directory / kStateFileName;
  bool rejected_corrupt = false;
  const Outcome<void> loaded = store->load_state(rejected_corrupt);
  if (!loaded) {
    if (rejected_corrupt && options.allow_state_discard_on_corruption) {
      store->report_.state_rejected = true;
      store->report_.requires_revalidation = true;
      store->report_.diagnostics.emplace_back("compacted state was discarded after explicit consent");
    } else {
      return loaded.error();
    }
  }

  CFN_RETURN_IF_ERROR(store->replay_journal());

  const bool first_boot = !store->report_.state_present && store->report_.journal_records_scanned == 0;
  store->report_.previous_epoch = store->state_.epoch;
  store->report_.previous_boot = store->state_.boot;

  if (first_boot) {
    store->epoch_ = FabricEpoch::initial();
  } else if (store->state_.clean_shutdown) {
    store->epoch_ = store->state_.epoch.valid() ? store->state_.epoch : FabricEpoch::initial();
  } else {
    const std::optional<FabricEpoch> next = store->state_.epoch.valid()
                                                ? store->state_.epoch.try_next()
                                                : std::optional<FabricEpoch>(FabricEpoch::initial());
    if (!next.has_value()) {
      return Error(ErrorCode::Overflow, "fabric epoch counter is exhausted");
    }
    store->epoch_ = *next;
  }
  const std::optional<BootIncarnation> next_boot = store->state_.boot.valid()
                                                      ? store->state_.boot.try_next()
                                                      : std::optional<BootIncarnation>(
                                                            BootIncarnation::initial());
  if (!next_boot.has_value()) {
    return Error(ErrorCode::Overflow, "boot incarnation counter is exhausted");
  }
  store->boot_ = *next_boot;
  store->report_.current_epoch = store->epoch_;
  store->report_.current_boot = store->boot_;
  store->report_.epoch_advanced = store->epoch_ != store->report_.previous_epoch;

  // Fence every persisted live authority claim and demand revalidation of every
  // external evidence reference. Neither may survive a restart.
  store->report_.live_authority_fenced = static_cast<std::uint32_t>(store->state_.live_authority.size());
  for (auto& entry : store->state_.evidence_references) {
    if (!entry.second.requires_revalidation) {
      entry.second.requires_revalidation = true;
    }
  }
  store->report_.evidence_requiring_revalidation =
      static_cast<std::uint32_t>(store->state_.evidence_references.size());
  store->report_.requires_revalidation = true;

  store->report_.policies_loaded = static_cast<std::uint32_t>(store->state_.policies.size());
  store->report_.demand_shapes_loaded = static_cast<std::uint32_t>(store->state_.demand_shapes.size());
  store->report_.models_loaded = static_cast<std::uint32_t>(store->state_.models.size());
  store->report_.history_loaded = static_cast<std::uint32_t>(store->state_.history.size());

  JournalScanResult scan;
  Outcome<Journal> journal = Journal::open(options.directory / kJournalFileName, options.limits, &scan);
  if (!journal) {
    return journal.error();
  }
  store->journal_ = std::make_unique<Journal>(std::move(*journal));

  // Record the new incarnation durably before anything can be acknowledged.
  {
    serialize::Writer writer;
    writer.epoch(store->epoch_);
    writer.boot(store->boot_);
    writer.boolean(false);
    std::vector<JournalRecord> records;
    JournalRecord record;
    record.kind = JournalRecordKind::EpochAdvance;
    record.payload = writer.take();
    records.push_back(std::move(record));
    const Outcome<void> appended = store->journal_->append_transaction(records);
    if (!appended) {
      return appended.error();
    }
    if (options.durability_barrier) {
      CFN_RETURN_IF_ERROR(store->journal_->sync());
    }
  }

  store->state_.epoch = store->epoch_;
  store->state_.boot = store->boot_;
  store->state_.clean_shutdown = false;
  store->report_.opened = true;
  return store;
}

Outcome<void> CapacityStore::load_state(bool& rejected_corrupt) {
  const std::filesystem::path state_path = options_.directory / kStateFileName;
  std::error_code exists_error;
  const bool present = std::filesystem::exists(state_path, exists_error);
  if (exists_error) {
    return Error(ErrorCode::IoError, "failed to inspect the durable state file",
                 options_.directory.string());
  }
  if (!present) {
    report_.state_present = false;
    return Outcome<void>();
  }
  report_.state_present = true;
  const Outcome<std::vector<std::byte>> data =
      read_whole_file(state_path, options_.limits.max_journal_bytes);
  if (!data) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = std::string(data.error().message());
    return data.error();
  }
  if (data->size() < kStateHeaderBytes) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = "state file is shorter than its header";
    return Error(ErrorCode::Truncated, "compacted state file is truncated", state_path.string());
  }
  const std::byte* header = data->data();
  if (get_u32(header) != kStateMagic) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = "state file magic does not match";
    return Error(ErrorCode::Corrupt, "compacted state file has an invalid magic number",
                 state_path.string());
  }
  if (get_u16(header + 4) != format_version) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = "state file format version does not match this build";
    return Error(ErrorCode::VersionMismatch, "compacted state file has a different format version",
                 state_path.string());
  }
  if (crc32c::compute(header, 20) != get_u32(header + 20)) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = "state file header checksum does not match";
    return Error(ErrorCode::ChecksumMismatch, "compacted state file header is corrupt",
                 state_path.string());
  }
  const std::uint64_t payload_len = get_u64(header + 8);
  if (payload_len != data->size() - kStateHeaderBytes) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = "state file payload length does not match the file size";
    return Error(ErrorCode::Truncated, "compacted state file payload is truncated",
                 state_path.string());
  }
  const std::span<const std::byte> payload(data->data() + kStateHeaderBytes,
                                           static_cast<std::size_t>(payload_len));
  if (crc32c::compute(payload) != get_u32(header + 16)) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = "state file payload checksum does not match";
    return Error(ErrorCode::ChecksumMismatch, "compacted state file payload is corrupt",
                 state_path.string());
  }
  Outcome<StoreState> decoded = decode_store_state(payload, options_.limits);
  if (!decoded) {
    rejected_corrupt = true;
    report_.state_rejected = true;
    report_.state_rejection_detail = std::string(decoded.error().message());
    return decoded.error();
  }
  state_ = std::move(*decoded);
  report_.state_accepted = true;
  return Outcome<void>();
}

Outcome<void> CapacityStore::replay_journal() {
  const Outcome<JournalScanResult> scan =
      scan_journal(options_.directory / kJournalFileName, options_.limits);
  if (!scan) {
    return scan.error();
  }
  report_.journal_records_scanned = scan->records_scanned;
  report_.journal_records_rejected = scan->records_rejected;
  report_.unfinished_attempts = scan->unfinished_attempts;
  report_.corrupt_records = scan->corrupt_records;
  report_.transactions_applied = scan->transactions_applied;
  report_.journal_truncated_tail = scan->truncated_tail;
  report_.journal_version_mismatch = scan->version_mismatch;
  report_.journal_header_invalid = scan->header_invalid;
  for (const std::string& diagnostic : scan->diagnostics) {
    if (report_.diagnostics.size() < options_.limits.max_diagnostics) {
      report_.diagnostics.push_back(diagnostic);
    }
  }
  if (scan->header_invalid || scan->version_mismatch) {
    report_.requires_revalidation = true;
    report_.state_rejected = report_.state_rejected || true;
    return Error(ErrorCode::Corrupt,
                 "the journal cannot be read by this build; durable history is ambiguous");
  }

  bool clean = false;
  std::uint64_t applied = 0;
  for (const JournalRecord& record : scan->records) {
    if (record.sequence <= state_.last_sequence) {
      continue;
    }
    switch (record.kind) {
      case JournalRecordKind::PolicyPut: {
        serialize::Reader reader(record.payload);
        CapacityPolicy value;
        CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
        state_.policies[value.id] = value;
        break;
      }
      case JournalRecordKind::PolicyRemove: {
        serialize::Reader reader(record.payload);
        PolicyId id;
        if (!reader.id(id)) {
          return Error(ErrorCode::Truncated, "policy removal record is malformed");
        }
        state_.policies.erase(id);
        break;
      }
      case JournalRecordKind::DemandShapePut: {
        serialize::Reader reader(record.payload);
        DemandShape value;
        CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
        state_.demand_shapes[value.id] = value;
        break;
      }
      case JournalRecordKind::DemandShapeRemove: {
        serialize::Reader reader(record.payload);
        DemandShapeId id;
        if (!reader.id(id)) {
          return Error(ErrorCode::Truncated, "demand shape removal record is malformed");
        }
        state_.demand_shapes.erase(id);
        break;
      }
      case JournalRecordKind::ModelPut: {
        serialize::Reader reader(record.payload);
        CapacityModel value;
        CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
        state_.models[value.id] = value;
        break;
      }
      case JournalRecordKind::ModelRemove: {
        serialize::Reader reader(record.payload);
        CapacityModelId id;
        if (!reader.id(id)) {
          return Error(ErrorCode::Truncated, "model removal record is malformed");
        }
        state_.models.erase(id);
        break;
      }
      case JournalRecordKind::HistoryAppend: {
        serialize::Reader reader(record.payload);
        SnapshotRecord value;
        CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
        state_.history.push_back(value);
        while (state_.history.size() > options_.limits.max_history_entries) {
          state_.history.erase(state_.history.begin());
        }
        break;
      }
      case JournalRecordKind::EvidenceReferencePut: {
        serialize::Reader reader(record.payload);
        EvidenceReference value;
        CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
        state_.evidence_references[value.source] = value;
        break;
      }
      case JournalRecordKind::LiveAuthorityGrant: {
        serialize::Reader reader(record.payload);
        LiveAuthorityRecord value;
        CFN_RETURN_IF_ERROR(serialize::decode(reader, value));
        state_.live_authority[value.holder] = value;
        break;
      }
      case JournalRecordKind::EpochAdvance: {
        serialize::Reader reader(record.payload);
        FabricEpoch epoch;
        BootIncarnation boot;
        bool clean_flag = false;
        if (!reader.epoch(epoch) || !reader.boot(boot) || !reader.boolean(clean_flag)) {
          return Error(ErrorCode::Truncated, "epoch record is malformed");
        }
        if (epoch.valid()) {
          state_.epoch = epoch;
        }
        if (boot.valid()) {
          state_.boot = boot;
        }
        clean = clean_flag;
        break;
      }
      case JournalRecordKind::CleanShutdown: {
        clean = true;
        break;
      }
      case JournalRecordKind::HistoryTrim:
      case JournalRecordKind::ObservationAppend:
      case JournalRecordKind::ObservationClear:
      case JournalRecordKind::Begin:
      case JournalRecordKind::Commit:
      case JournalRecordKind::Invalid:
      default:
        return Error(ErrorCode::Unsupported, "journal carries a record kind this build cannot apply");
    }
    applied += 1;
    if (record.sequence > state_.last_sequence) {
      state_.last_sequence = record.sequence;
    }
  }
  report_.journal_records_applied = applied;
  state_.clean_shutdown = clean;
  return Outcome<void>();
}

std::map<PolicyId, CapacityPolicy> CapacityStore::policies() const {
  const std::lock_guard<std::mutex> guard(mutex_);
  return state_.policies;
}

std::map<DemandShapeId, DemandShape> CapacityStore::demand_shapes() const {
  const std::lock_guard<std::mutex> guard(mutex_);
  return state_.demand_shapes;
}

std::map<CapacityModelId, CapacityModel> CapacityStore::models() const {
  const std::lock_guard<std::mutex> guard(mutex_);
  return state_.models;
}

std::vector<SnapshotRecord> CapacityStore::history() const {
  const std::lock_guard<std::mutex> guard(mutex_);
  return state_.history;
}

std::map<EvidenceSourceId, EvidenceReference> CapacityStore::evidence_references() const {
  const std::lock_guard<std::mutex> guard(mutex_);
  return state_.evidence_references;
}

std::map<GenericId, LiveAuthorityRecord> CapacityStore::live_authority() const {
  const std::lock_guard<std::mutex> guard(mutex_);
  return state_.live_authority;
}

Outcome<void> CapacityStore::put_policy(const CapacityPolicy& policy) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  serialize::encode(writer, policy);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::PolicyPut;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.policies[policy.id] = policy;
  return Outcome<void>();
}

Outcome<void> CapacityStore::remove_policy(const PolicyId& id) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  writer.id(id);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::PolicyRemove;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.policies.erase(id);
  return Outcome<void>();
}

Outcome<void> CapacityStore::put_demand_shape(const DemandShape& shape) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  serialize::encode(writer, shape);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::DemandShapePut;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.demand_shapes[shape.id] = shape;
  return Outcome<void>();
}

Outcome<void> CapacityStore::remove_demand_shape(const DemandShapeId& id) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  writer.id(id);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::DemandShapeRemove;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.demand_shapes.erase(id);
  return Outcome<void>();
}

Outcome<void> CapacityStore::put_model(const CapacityModel& model) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  serialize::encode(writer, model);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::ModelPut;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.models[model.id] = model;
  return Outcome<void>();
}

Outcome<void> CapacityStore::remove_model(const CapacityModelId& id) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  writer.id(id);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::ModelRemove;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.models.erase(id);
  return Outcome<void>();
}

Outcome<void> CapacityStore::append_history(const SnapshotRecord& record) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  serialize::encode(writer, record);
  std::vector<JournalRecord> records;
  JournalRecord entry;
  entry.kind = JournalRecordKind::HistoryAppend;
  entry.payload = writer.take();
  records.push_back(std::move(entry));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.history.push_back(record);
  while (state_.history.size() > options_.limits.max_history_entries) {
    state_.history.erase(state_.history.begin());
  }
  return Outcome<void>();
}

Outcome<void> CapacityStore::put_evidence_reference(const EvidenceReference& reference) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  serialize::encode(writer, reference);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::EvidenceReferencePut;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.evidence_references[reference.source] = reference;
  return Outcome<void>();
}

Outcome<void> CapacityStore::grant_live_authority(const LiveAuthorityRecord& record) {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  serialize::Writer writer;
  serialize::encode(writer, record);
  std::vector<JournalRecord> records;
  JournalRecord entry;
  entry.kind = JournalRecordKind::LiveAuthorityGrant;
  entry.payload = writer.take();
  records.push_back(std::move(entry));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  state_.live_authority[record.holder] = record;
  return Outcome<void>();
}

Outcome<void> CapacityStore::advance_epoch() {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  const std::optional<FabricEpoch> next = epoch_.try_next();
  if (!next.has_value()) {
    return Error(ErrorCode::Overflow, "fabric epoch counter is exhausted");
  }
  serialize::Writer writer;
  writer.epoch(*next);
  writer.boot(boot_);
  writer.boolean(false);
  std::vector<JournalRecord> records;
  JournalRecord record;
  record.kind = JournalRecordKind::EpochAdvance;
  record.payload = writer.take();
  records.push_back(std::move(record));
  CFN_RETURN_IF_ERROR(journal_->append_transaction(records));
  if (options_.durability_barrier) {
    CFN_RETURN_IF_ERROR(journal_->sync());
  }
  state_.last_sequence = journal_->next_sequence() - 1ULL;
  epoch_ = *next;
  state_.epoch = epoch_;
  state_.boot = boot_;
  report_.current_epoch = epoch_;
  report_.epoch_advanced = true;
  return Outcome<void>();
}

bool CapacityStore::live_authority_valid(const GenericId& holder, Timestamp now) const {
  const std::lock_guard<std::mutex> guard(mutex_);
  const auto found = state_.live_authority.find(holder);
  if (found == state_.live_authority.end()) {
    return false;
  }
  const LiveAuthorityRecord& record = found->second;
  if (record.boot != boot_ || record.epoch != epoch_) {
    return false;
  }
  Duration age{};
  if (!elapsed(now, record.granted_at, age)) {
    return false;
  }
  if (age.nanos < 0) {
    return false;
  }
  return age.nanos < record.ttl.nanos;
}

Outcome<void> CapacityStore::compact() {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Error(ErrorCode::Closed, "the durable store is closed");
  }
  StoreState snapshot = state_;
  snapshot.epoch = epoch_;
  snapshot.boot = boot_;
  snapshot.clean_shutdown = false;

  const Outcome<std::vector<std::byte>> payload = encode_store_state(snapshot, options_.limits);
  if (!payload) {
    return payload.error();
  }
  std::vector<std::byte> file;
  file.resize(kStateHeaderBytes + payload->size());
  put_u32(file.data(), kStateMagic);
  put_u16(file.data() + 4, format_version);
  put_u16(file.data() + 6, 0);
  put_u64(file.data() + 8, static_cast<std::uint64_t>(payload->size()));
  put_u32(file.data() + 16, crc32c::compute(*payload));
  put_u32(file.data() + 20, crc32c::compute(file.data(), 20));
  for (std::size_t index = 0; index < payload->size(); ++index) {
    file[kStateHeaderBytes + index] = (*payload)[index];
  }

  const std::filesystem::path temporary = options_.directory / kStateTempName;
  const std::filesystem::path target = options_.directory / kStateFileName;
  std::error_code remove_error;
  std::filesystem::remove(temporary, remove_error);
  CFN_RETURN_IF_ERROR(write_whole_file(temporary, file, options_.durability_barrier));
  std::error_code rename_error;
  std::filesystem::rename(temporary, target, rename_error);
  if (rename_error) {
    std::filesystem::remove(temporary, remove_error);
    return Error(ErrorCode::IoError, "failed to publish the compacted state", target.string());
  }
  CFN_RETURN_IF_ERROR(sync_directory(options_.directory));

  // Only now may the journal be reset: the checkpoint is durable.
  journal_->close();
  journal_.reset();
  std::error_code journal_error;
  std::filesystem::remove(options_.directory / kJournalFileName, journal_error);
  JournalScanResult scan;
  Outcome<Journal> journal =
      Journal::open(options_.directory / kJournalFileName, options_.limits, &scan);
  if (!journal) {
    return journal.error();
  }
  journal_ = std::make_unique<Journal>(std::move(*journal));
  // The fresh journal continues the numbering the checkpoint established, so a
  // record written after compaction is always distinguishable from one the
  // checkpoint already folded in - including across a crash in the window
  // between publishing the checkpoint and resetting the journal.
  journal_->set_next_sequence(state_.last_sequence + 1ULL);
  CFN_RETURN_IF_ERROR(sync_directory(options_.directory));
  return Outcome<void>();
}

Outcome<void> CapacityStore::close() {
  const std::lock_guard<std::mutex> guard(mutex_);
  if (closed_) {
    return Outcome<void>();
  }
  closed_ = true;
  Outcome<void> result;
  if (journal_ != nullptr) {
    serialize::Writer writer;
    writer.epoch(epoch_);
    writer.boot(boot_);
    writer.boolean(true);
    std::vector<JournalRecord> records;
    JournalRecord record;
    record.kind = JournalRecordKind::CleanShutdown;
    record.payload = writer.take();
    records.push_back(std::move(record));
    const Outcome<void> appended = journal_->append_transaction(records);
    if (!appended) {
      result = appended.error();
    } else if (options_.durability_barrier) {
      const Outcome<void> synced = journal_->sync();
      if (!synced) {
        result = synced.error();
      }
    }
    state_.last_sequence = journal_->next_sequence() - 1ULL;
    state_.clean_shutdown = true;
    journal_->close();
    journal_.reset();
  }
  return result;
}

}  // namespace cfn