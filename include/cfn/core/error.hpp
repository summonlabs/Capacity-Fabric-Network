// Capacity Fabric Network - error taxonomy.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CORE_ERROR_HPP
#define CFN_CORE_ERROR_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cfn/core/api.hpp"

namespace cfn {

inline constexpr std::size_t kMaxErrorTextBytes = 512;

enum class ErrorCode : std::uint16_t {
  Ok = 0,
  InvalidArgument = 1,
  MalformedInput = 2,
  NotFound = 3,
  Duplicate = 4,
  GenerationMismatch = 5,
  EpochMismatch = 6,
  StaleTopology = 7,
  StaleReservation = 8,
  StalePolicy = 9,
  StaleEvidence = 10,
  StaleModel = 11,
  StaleResource = 12,
  StaleFailureDomain = 13,
  StaleDemandShape = 14,
  Overflow = 15,
  Underflow = 16,
  Contradictory = 17,
  LimitExceeded = 18,
  Unsupported = 19,
  Corrupt = 20,
  Truncated = 21,
  VersionMismatch = 22,
  ChecksumMismatch = 23,
  IoError = 24,
  NotAuthoritative = 25,
  Cancelled = 26,
  InvalidState = 27,
  TransportError = 28,
  Closed = 29,
  Unknown = 30,
  Disconnected = 31,
  NotEvaluated = 32,
  RequiresRevalidation = 33,
  Busy = 34,
};

CFN_API std::string_view to_string(ErrorCode code) noexcept;
CFN_API bool is_error(ErrorCode code) noexcept;

class CFN_API Error {
 public:
  Error() = default;
  Error(ErrorCode code, std::string_view message);
  Error(ErrorCode code, std::string_view message, std::string_view subject);

  ErrorCode code() const noexcept { return code_; }
  std::string_view message() const noexcept { return message_; }
  std::string_view subject() const noexcept { return subject_; }
  bool ok() const noexcept { return code_ == ErrorCode::Ok; }

  std::string to_text() const;
  void set_subject(std::string_view subject);

 private:
  ErrorCode code_ = ErrorCode::Ok;
  std::string message_;
  std::string subject_;
};

CFN_API std::string bound_text(std::string_view text, std::size_t max_bytes = kMaxErrorTextBytes);

class CFN_API OutcomeAccessError : public std::logic_error {
 public:
  explicit OutcomeAccessError(const Error& error);
  const Error& error() const noexcept { return error_; }

 private:
  Error error_;
};

}  // namespace cfn

#endif  // CFN_CORE_ERROR_HPP
