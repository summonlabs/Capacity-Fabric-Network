// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/persist/journal.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

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

constexpr std::uint32_t kJournalMagic = 0x4A4E4643u;
constexpr std::uint32_t kRecordMagic = 0x524E4643u;
constexpr std::size_t kJournalHeaderBytes = 32;
constexpr std::size_t kRecordHeaderBytes = 28;

void put_u16(std::array<std::byte, kRecordHeaderBytes>& buffer, std::size_t offset,
             std::uint16_t value) {
  for (std::size_t index = 0; index < 2; ++index) {
    buffer[offset + index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
  }
}

void put_u32(std::array<std::byte, kRecordHeaderBytes>& buffer, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    buffer[offset + index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
  }
}

void put_u64(std::array<std::byte, kRecordHeaderBytes>& buffer, std::size_t offset,
             std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    buffer[offset + index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
  }
}

[[nodiscard]] std::uint16_t get_u16(const std::byte* data) {
  std::uint16_t value = 0;
  for (std::size_t index = 0; index < 2; ++index) {
    value |= static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[index])) << (8U * index);
  }
  return value;
}

[[nodiscard]] std::uint32_t get_u32(const std::byte* data) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[index])) << (8U * index);
  }
  return value;
}

[[nodiscard]] std::uint64_t get_u64(const std::byte* data) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[index])) << (8U * index);
  }
  return value;
}

[[nodiscard]] Outcome<void> sync_file(std::FILE* file) {
  if (std::fflush(file) != 0) {
    return Error(ErrorCode::IoError, "failed to flush the journal");
  }
#if defined(_MSC_VER)
  if (_commit(_fileno(file)) != 0) {
    return Error(ErrorCode::IoError, "failed to commit the journal to stable storage");
  }
#else
  if (fsync(fileno(file)) != 0) {
    return Error(ErrorCode::IoError, "failed to commit the journal to stable storage");
  }
#endif
  return Outcome<void>();
}

[[nodiscard]] Outcome<void> truncate_file(std::FILE* file, std::uint64_t size) {
  if (std::fflush(file) != 0) {
    return Error(ErrorCode::IoError, "failed to flush before truncating the journal");
  }
#if defined(_MSC_VER)
  if (_chsize_s(_fileno(file), static_cast<__int64>(size)) != 0) {
    return Error(ErrorCode::IoError, "failed to truncate the journal");
  }
#else
  if (ftruncate(fileno(file), static_cast<off_t>(size)) != 0) {
    return Error(ErrorCode::IoError, "failed to truncate the journal");
  }
#endif
  return Outcome<void>();
}

[[nodiscard]] Outcome<void> write_bytes(std::FILE* file, const void* data, std::size_t size) {
  if (size == 0) {
    return Outcome<void>();
  }
  if (std::fwrite(data, 1, size, file) != size) {
    return Error(ErrorCode::IoError, "failed to write to the journal");
  }
  return Outcome<void>();
}

[[nodiscard]] Outcome<std::size_t> read_exact(std::FILE* file, void* data, std::size_t size) {
  const std::size_t read = std::fread(data, 1, size, file);
  if (read != size) {
    if (std::ferror(file) != 0) {
      return Error(ErrorCode::IoError, "failed to read the journal");
    }
    return read;
  }
  return read;
}

[[nodiscard]] std::array<std::byte, kJournalHeaderBytes> build_header(FabricEpoch epoch) {
  std::array<std::byte, kJournalHeaderBytes> header{};
  std::array<std::byte, kRecordHeaderBytes> scratch{};
  put_u32(scratch, 0, kJournalMagic);
  put_u16(scratch, 4, format_version);
  put_u16(scratch, 6, 0);
  put_u32(scratch, 8, static_cast<std::uint32_t>(kJournalHeaderBytes));
  for (std::size_t index = 0; index < 12; ++index) {
    header[index] = scratch[index];
  }
  put_u64(scratch, 0, epoch.value());
  for (std::size_t index = 0; index < 8; ++index) {
    header[12 + index] = scratch[index];
  }
  const std::uint32_t checksum = crc32c::compute(header.data(), 20);
  put_u32(scratch, 0, checksum);
  for (std::size_t index = 0; index < 4; ++index) {
    header[20 + index] = scratch[index];
  }
  return header;
}

[[nodiscard]] std::array<std::byte, kRecordHeaderBytes> build_record_header(JournalRecordKind kind,
                                                                           std::uint32_t payload_len,
                                                                           std::uint64_t sequence,
                                                                           std::uint32_t payload_crc) {
  std::array<std::byte, kRecordHeaderBytes> header{};
  put_u32(header, 0, kRecordMagic);
  put_u16(header, 4, static_cast<std::uint16_t>(kind));
  put_u16(header, 6, 0);
  put_u32(header, 8, payload_len);
  put_u64(header, 12, sequence);
  put_u32(header, 20, payload_crc);
  put_u32(header, 24, 0);
  const std::uint32_t header_crc = crc32c::compute(header.data(), 24);
  put_u32(header, 24, header_crc);
  return header;
}

struct TransactionBuffer {
  bool active = false;
  std::uint64_t id = 0;
  std::uint32_t expected = 0;
  std::vector<JournalRecord> records;
  std::uint32_t chain = crc32c::kInitial;
};

void fold_chain(std::uint32_t& chain, std::uint32_t payload_crc) {
  std::array<std::byte, 4> bytes{};
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[index] = static_cast<std::byte>((payload_crc >> (8U * index)) & 0xFFU);
  }
  chain = crc32c::update(chain, bytes.data(), bytes.size());
}

}  // namespace

Outcome<JournalScanResult> scan_journal(const std::filesystem::path& path, const Limits& limits) {
  JournalScanResult result;
  std::FILE* file = nullptr;
#if defined(_MSC_VER)
  if (fopen_s(&file, path.string().c_str(), "rb") != 0) {
    file = nullptr;
  }
#else
  file = std::fopen(path.string().c_str(), "rb");
#endif
  if (file == nullptr) {
    // A missing journal is an empty journal, not a failure.
    result.valid_bytes = 0;
    return result;
  }

  std::array<std::byte, kJournalHeaderBytes> header{};
  const Outcome<std::size_t> header_read = read_exact(file, header.data(), header.size());
  if (!header_read) {
    std::fclose(file);
    return header_read.error();
  }
  if (*header_read == 0) {
    std::fclose(file);
    return result;
  }
  if (*header_read != header.size()) {
    result.header_invalid = true;
    result.diagnostics.emplace_back("journal header is truncated");
    std::fclose(file);
    return result;
  }
  if (get_u32(header.data()) != kJournalMagic) {
    result.header_invalid = true;
    result.diagnostics.emplace_back("journal magic does not match");
    std::fclose(file);
    return result;
  }
  const std::uint32_t stored_header_crc = get_u32(header.data() + 20);
  if (crc32c::compute(header.data(), 20) != stored_header_crc) {
    result.header_invalid = true;
    result.diagnostics.emplace_back("journal header checksum does not match");
    std::fclose(file);
    return result;
  }
  if (get_u16(header.data() + 4) != format_version) {
    result.version_mismatch = true;
    result.diagnostics.emplace_back("journal format version does not match this build");
    std::fclose(file);
    return result;
  }

  std::uint64_t offset = kJournalHeaderBytes;
  result.valid_bytes = offset;
  TransactionBuffer transaction;

  for (;;) {
    std::array<std::byte, kRecordHeaderBytes> record_header{};
    const Outcome<std::size_t> record_read = read_exact(file, record_header.data(), record_header.size());
    if (!record_read) {
      std::fclose(file);
      return record_read.error();
    }
    if (*record_read == 0) {
      break;
    }
    if (*record_read != record_header.size() || get_u32(record_header.data()) != kRecordMagic ||
        crc32c::compute(record_header.data(), 24) != get_u32(record_header.data() + 24)) {
      result.truncated_tail = true;
      result.diagnostics.emplace_back("journal record header is torn or invalid; the tail is discarded");
      break;
    }
    const std::uint32_t payload_len = get_u32(record_header.data() + 8);
    const std::uint64_t sequence = get_u64(record_header.data() + 12);
    const std::uint32_t payload_crc = get_u32(record_header.data() + 20);
    if (payload_len > limits.max_journal_record_bytes) {
      result.corrupt_records += 1;
      result.truncated_tail = true;
      result.diagnostics.emplace_back("journal record declares a payload larger than the limit");
      break;
    }
    std::vector<std::byte> payload(payload_len);
    if (payload_len != 0) {
      const Outcome<std::size_t> payload_read = read_exact(file, payload.data(), payload_len);
      if (!payload_read) {
        std::fclose(file);
        return payload_read.error();
      }
      if (*payload_read != payload_len) {
        result.truncated_tail = true;
        result.diagnostics.emplace_back("journal record payload is truncated");
        break;
      }
    }
    if (crc32c::compute(payload.data(), payload.size()) != payload_crc) {
      result.corrupt_records += 1;
      result.truncated_tail = true;
      result.diagnostics.emplace_back("journal record payload checksum does not match");
      break;
    }

    result.records_scanned += 1;
    if (sequence > result.last_sequence) {
      result.last_sequence = sequence;
    }
    offset += kRecordHeaderBytes + payload_len;

    const auto kind = static_cast<JournalRecordKind>(get_u16(record_header.data() + 4));
    if (kind == JournalRecordKind::Begin) {
      if (transaction.active) {
        result.unfinished_attempts += 1;
        result.records_rejected += transaction.records.size();
        result.diagnostics.emplace_back("a transaction was started before the previous one committed");
      }
      transaction = TransactionBuffer{};
      transaction.active = true;
      transaction.chain = crc32c::kInitial;
      if (payload.size() >= 12) {
        transaction.id = get_u64(payload.data());
        transaction.expected = get_u32(payload.data() + 8);
      }
      continue;
    }
    if (kind == JournalRecordKind::Commit) {
      bool accepted = false;
      if (transaction.active && payload.size() >= 16) {
        const std::uint64_t id = get_u64(payload.data());
        const std::uint32_t count = get_u32(payload.data() + 8);
        const std::uint32_t chain = get_u32(payload.data() + 12);
        accepted = id == transaction.id && count == transaction.expected &&
                   count == transaction.records.size() && chain == (transaction.chain ^ 0xFFFFFFFFu);
      }
      if (accepted) {
        for (JournalRecord& record : transaction.records) {
          result.records.push_back(std::move(record));
        }
        result.records_applied += transaction.records.size();
        result.transactions_applied += 1;
        result.valid_bytes = offset;
      } else {
        result.unfinished_attempts += 1;
        result.records_rejected += transaction.records.size();
        result.diagnostics.emplace_back("a transaction did not verify and was discarded");
      }
      transaction = TransactionBuffer{};
      continue;
    }

    JournalRecord record;
    record.kind = kind;
    record.sequence = sequence;
    record.payload = std::move(payload);
    if (transaction.active) {
      transaction.records.push_back(std::move(record));
      fold_chain(transaction.chain, payload_crc);
    } else {
      // Records written outside a transaction commit themselves.
      result.records.push_back(std::move(record));
      result.records_applied += 1;
      result.valid_bytes = offset;
    }
  }

  if (transaction.active) {
    result.unfinished_attempts += 1;
    result.records_rejected += transaction.records.size();
    result.diagnostics.emplace_back("the journal ends inside a transaction");
  }
  std::fclose(file);
  return result;
}

Journal::Journal(Journal&& other) noexcept
    : path_(std::move(other.path_)),
      file_(other.file_),
      next_sequence_(other.next_sequence_),
      bytes_written_(other.bytes_written_),
      record_count_(other.record_count_),
      fsync_(other.fsync_) {
  other.file_ = nullptr;
}

Journal& Journal::operator=(Journal&& other) noexcept {
  if (this != &other) {
    close();
    path_ = std::move(other.path_);
    file_ = other.file_;
    next_sequence_ = other.next_sequence_;
    bytes_written_ = other.bytes_written_;
    record_count_ = other.record_count_;
    fsync_ = other.fsync_;
    other.file_ = nullptr;
  }
  return *this;
}

void Journal::close() {
  if (file_ != nullptr) {
    (void)std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
  }
}

Outcome<Journal> Journal::open(const std::filesystem::path& path, const Limits& limits,
                               JournalScanResult* scan_out) {
  const Outcome<JournalScanResult> scan = scan_journal(path, limits);
  if (!scan) {
    return scan.error();
  }
  Journal journal;
  journal.path_ = path;
  journal.next_sequence_ = scan->last_sequence + 1ULL;
  journal.record_count_ = scan->records_scanned;

  const bool existing = std::filesystem::exists(path);
  if (!existing) {
#if defined(_MSC_VER)
    if (fopen_s(&journal.file_, path.string().c_str(), "w+b") != 0) {
      journal.file_ = nullptr;
    }
#else
    journal.file_ = std::fopen(path.string().c_str(), "w+b");
#endif
    if (journal.file_ == nullptr) {
      return Error(ErrorCode::IoError, "failed to create the journal", path.string());
    }
    const std::array<std::byte, kJournalHeaderBytes> header =
        build_header(FabricEpoch::initial());
    CFN_RETURN_IF_ERROR(write_bytes(journal.file_, header.data(), header.size()));
    CFN_RETURN_IF_ERROR(sync_file(journal.file_));
    journal.bytes_written_ = header.size();
  } else {
    if (scan->header_invalid || scan->version_mismatch) {
      return Error(ErrorCode::Corrupt,
                   "the existing journal cannot be read by this build", path.string());
    }
#if defined(_MSC_VER)
    if (fopen_s(&journal.file_, path.string().c_str(), "r+b") != 0) {
      journal.file_ = nullptr;
    }
#else
    journal.file_ = std::fopen(path.string().c_str(), "r+b");
#endif
    if (journal.file_ == nullptr) {
      return Error(ErrorCode::IoError, "failed to open the journal for append", path.string());
    }
    if (scan->truncated_tail) {
      CFN_RETURN_IF_ERROR(truncate_file(journal.file_, scan->valid_bytes));
      CFN_RETURN_IF_ERROR(sync_file(journal.file_));
    }
    const long seek_result = std::fseek(journal.file_, 0, SEEK_END);
    if (seek_result != 0) {
      return Error(ErrorCode::IoError, "failed to seek to the end of the journal", path.string());
    }
    const long position = std::ftell(journal.file_);
    journal.bytes_written_ = position > 0 ? static_cast<std::uint64_t>(position) : 0ULL;
  }
  if (scan_out != nullptr) {
    *scan_out = *scan;
  }
  return journal;
}

bool Journal::needs_rotation(const Limits& limits) const noexcept {
  return bytes_written_ >= limits.max_journal_bytes || record_count_ >= limits.max_journal_records;
}

Outcome<void> Journal::append(JournalRecordKind kind, std::span<const std::byte> payload) {
  if (file_ == nullptr) {
    return Error(ErrorCode::Closed, "the journal is not open");
  }
  if (payload.size() > static_cast<std::size_t>(UINT32_MAX)) {
    return Error(ErrorCode::LimitExceeded, "journal payload exceeds the addressable size");
  }
  const std::uint32_t payload_crc = crc32c::compute(payload);
  const std::array<std::byte, kRecordHeaderBytes> header =
      build_record_header(kind, static_cast<std::uint32_t>(payload.size()), next_sequence_,
                          payload_crc);
  CFN_RETURN_IF_ERROR(write_bytes(file_, header.data(), header.size()));
  CFN_RETURN_IF_ERROR(write_bytes(file_, payload.data(), payload.size()));
  bytes_written_ += header.size() + payload.size();
  record_count_ += 1;
  next_sequence_ += 1;
  return Outcome<void>();
}

Outcome<void> Journal::append_transaction(const std::vector<JournalRecord>& records) {
  if (file_ == nullptr) {
    return Error(ErrorCode::Closed, "the journal is not open");
  }
  if (records.empty()) {
    return Error(ErrorCode::InvalidArgument, "a journal transaction must carry at least one record");
  }
  const std::uint64_t transaction_id = next_sequence_;
  std::vector<std::byte> begin_payload(12);
  for (std::size_t index = 0; index < 8; ++index) {
    begin_payload[index] = static_cast<std::byte>((transaction_id >> (8U * index)) & 0xFFU);
  }
  const std::uint32_t count = static_cast<std::uint32_t>(records.size());
  for (std::size_t index = 0; index < 4; ++index) {
    begin_payload[8 + index] = static_cast<std::byte>((count >> (8U * index)) & 0xFFU);
  }
  CFN_RETURN_IF_ERROR(append(JournalRecordKind::Begin, begin_payload));

  std::uint32_t chain = crc32c::kInitial;
  for (const JournalRecord& record : records) {
    fold_chain(chain, crc32c::compute(record.payload));
    CFN_RETURN_IF_ERROR(append(record.kind, record.payload));
  }

  std::vector<std::byte> commit_payload(16);
  for (std::size_t index = 0; index < 8; ++index) {
    commit_payload[index] = static_cast<std::byte>((transaction_id >> (8U * index)) & 0xFFU);
  }
  for (std::size_t index = 0; index < 4; ++index) {
    commit_payload[8 + index] = static_cast<std::byte>((count >> (8U * index)) & 0xFFU);
  }
  const std::uint32_t final_chain = chain ^ 0xFFFFFFFFu;
  for (std::size_t index = 0; index < 4; ++index) {
    commit_payload[12 + index] = static_cast<std::byte>((final_chain >> (8U * index)) & 0xFFU);
  }
  return append(JournalRecordKind::Commit, commit_payload);
}

Outcome<void> Journal::sync() {
  if (file_ == nullptr) {
    return Error(ErrorCode::Closed, "the journal is not open");
  }
  return sync_file(file_);
}

}  // namespace cfn
