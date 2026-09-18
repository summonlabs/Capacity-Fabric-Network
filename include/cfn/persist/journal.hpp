// Capacity Fabric Network - crash safe append-only journal.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Layout:
//   header  32 bytes: magic, format version, flags, header bytes, creating
//                     epoch, header CRC
//   record  28 bytes + payload: magic, kind, flags, payload length, sequence,
//                     payload CRC, header CRC
//
// A transaction is a Begin record, zero or more data records, and a Commit
// record carrying a chain checksum. Recovery applies a transaction only when
// its Commit is present and its chain checksum matches. Anything else - a torn
// tail, a truncated record, a corrupt payload - is reported as an unfinished
// attempt or a corrupt record and is never applied.
#ifndef CFN_PERSIST_JOURNAL_HPP
#define CFN_PERSIST_JOURNAL_HPP

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "cfn/core/identity.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/persist/records.hpp"

namespace cfn {

enum class JournalRecordKind : std::uint16_t {
  Invalid = 0,
  Begin = 1,
  Commit = 2,
  PolicyPut = 3,
  PolicyRemove = 4,
  DemandShapePut = 5,
  DemandShapeRemove = 6,
  ModelPut = 7,
  ModelRemove = 8,
  HistoryAppend = 9,
  HistoryTrim = 10,
  EvidenceReferencePut = 11,
  LiveAuthorityGrant = 12,
  EpochAdvance = 13,
  ObservationAppend = 14,
  ObservationClear = 15,
  CleanShutdown = 16,
};

struct JournalRecord {
  JournalRecordKind kind = JournalRecordKind::Invalid;
  std::uint64_t sequence = 0;
  std::vector<std::byte> payload;
};

struct JournalScanResult {
  std::vector<JournalRecord> records;
  std::uint64_t records_scanned = 0;
  std::uint64_t records_applied = 0;
  std::uint64_t records_rejected = 0;
  std::uint64_t unfinished_attempts = 0;
  std::uint64_t corrupt_records = 0;
  std::uint64_t transactions_applied = 0;
  bool truncated_tail = false;
  bool version_mismatch = false;
  bool header_invalid = false;
  /// Offset of the end of the last successfully committed transaction. A
  /// journal may be truncated to this offset to discard a torn tail.
  std::uint64_t valid_bytes = 0;
  std::uint64_t last_sequence = 0;
  std::vector<std::string> diagnostics;
};

/// Scans a journal file without modifying it.
[[nodiscard]] CFN_API Outcome<JournalScanResult> scan_journal(const std::filesystem::path& path,
                                                              const Limits& limits);

class CFN_API Journal {
 public:
  /// Opens (creating when absent) a journal for append. The existing contents
  /// are scanned first so that appends continue the sequence and so that a torn
  /// tail is truncated away.
  static Outcome<Journal> open(const std::filesystem::path& path, const Limits& limits,
                               JournalScanResult* scan_out);

  Journal(const Journal&) = delete;
  Journal& operator=(const Journal&) = delete;
  Journal(Journal&& other) noexcept;
  Journal& operator=(Journal&& other) noexcept;

  [[nodiscard]] Outcome<void> append(JournalRecordKind kind, std::span<const std::byte> payload);
  /// Writes Begin, the supplied records, and Commit as one atomic group.
  [[nodiscard]] Outcome<void> append_transaction(const std::vector<JournalRecord>& records);

  /// Flushes userspace buffers and asks the operating system to make the file
  /// durable. Must complete before a mutation is acknowledged.
  [[nodiscard]] Outcome<void> sync();

  [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }
  /// Continues numbering from a value chosen by the caller. Used only when a
  /// checkpoint supersedes the previous journal and a fresh file is started.
  void set_next_sequence(std::uint64_t sequence) noexcept {
    next_sequence_ = sequence == 0 ? 1 : sequence;
  }
  [[nodiscard]] std::uint64_t bytes_written() const noexcept { return bytes_written_; }
  [[nodiscard]] std::uint64_t record_count() const noexcept { return record_count_; }
  [[nodiscard]] bool needs_rotation(const Limits& limits) const noexcept;

  void close();

 private:
  Journal() = default;

  std::filesystem::path path_;
  std::FILE* file_ = nullptr;
  std::uint64_t next_sequence_ = 1;
  std::uint64_t bytes_written_ = 0;
  std::uint64_t record_count_ = 0;
  bool fsync_ = true;
};

}  // namespace cfn

#endif  // CFN_PERSIST_JOURNAL_HPP