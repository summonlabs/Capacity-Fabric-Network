// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/core/error.hpp"

#include <utility>

namespace cfn {

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::InvalidArgument: return "invalid-argument";
    case ErrorCode::MalformedInput: return "malformed-input";
    case ErrorCode::NotFound: return "not-found";
    case ErrorCode::Duplicate: return "duplicate";
    case ErrorCode::GenerationMismatch: return "generation-mismatch";
    case ErrorCode::EpochMismatch: return "epoch-mismatch";
    case ErrorCode::StaleTopology: return "stale-topology";
    case ErrorCode::StaleReservation: return "stale-reservation";
    case ErrorCode::StalePolicy: return "stale-policy";
    case ErrorCode::StaleEvidence: return "stale-evidence";
    case ErrorCode::StaleModel: return "stale-model";
    case ErrorCode::StaleResource: return "stale-resource";
    case ErrorCode::StaleFailureDomain: return "stale-failure-domain";
    case ErrorCode::StaleDemandShape: return "stale-demand-shape";
    case ErrorCode::Overflow: return "overflow";
    case ErrorCode::Underflow: return "underflow";
    case ErrorCode::Contradictory: return "contradictory";
    case ErrorCode::LimitExceeded: return "limit-exceeded";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::Corrupt: return "corrupt";
    case ErrorCode::Truncated: return "truncated";
    case ErrorCode::VersionMismatch: return "version-mismatch";
    case ErrorCode::ChecksumMismatch: return "checksum-mismatch";
    case ErrorCode::IoError: return "io-error";
    case ErrorCode::NotAuthoritative: return "not-authoritative";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::InvalidState: return "invalid-state";
    case ErrorCode::TransportError: return "transport-error";
    case ErrorCode::Closed: return "closed";
    case ErrorCode::Unknown: return "unknown";
    case ErrorCode::Disconnected: return "disconnected";
    case ErrorCode::NotEvaluated: return "not-evaluated";
    case ErrorCode::RequiresRevalidation: return "requires-revalidation";
    case ErrorCode::Busy: return "busy";
  }
  return "unrecognised";
}

bool is_error(ErrorCode code) noexcept { return code != ErrorCode::Ok; }

std::string bound_text(std::string_view text, std::size_t max_bytes) {
  std::string result;
  result.reserve(text.size() < max_bytes ? text.size() : max_bytes);
  for (const char character : text) {
    if (result.size() >= max_bytes) {
      break;
    }
    const unsigned char byte = static_cast<unsigned char>(character);
    result.push_back((byte < 0x20 || byte == 0x7F) ? '?' : character);
  }
  return result;
}

Error::Error(ErrorCode code, std::string_view message)
    : code_(code), message_(bound_text(message)) {}

Error::Error(ErrorCode code, std::string_view message, std::string_view subject)
    : code_(code), message_(bound_text(message)), subject_(bound_text(subject, 128)) {}

std::string Error::to_text() const {
  std::string result(to_string(code_));
  if (!message_.empty()) {
    result.append(": ");
    result.append(message_);
  }
  if (!subject_.empty()) {
    result.append(" [");
    result.append(subject_);
    result.append("]");
  }
  return result;
}

void Error::set_subject(std::string_view subject) { subject_ = bound_text(subject, 128); }

OutcomeAccessError::OutcomeAccessError(const Error& error)
    : std::logic_error(std::string("outcome has no value: ").append(error.to_text())), error_(error) {}

}  // namespace cfn
