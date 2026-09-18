// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/model/policy.hpp"

namespace cfn {

std::string_view to_string(ResilienceMode value) noexcept {
  switch (value) {
    case ResilienceMode::None: return "none";
    case ResilienceMode::DomainN1: return "domain-n-1";
    case ResilienceMode::DomainN1N1: return "domain-n-1-1";
  }
  return "unknown";
}

bool resilience_mode_from_string(std::string_view text, ResilienceMode& out) noexcept {
  if (text == "none") { out = ResilienceMode::None; return true; }
  if (text == "n1" || text == "domain-n-1") { out = ResilienceMode::DomainN1; return true; }
  if (text == "n1n1" || text == "domain-n-1-1") { out = ResilienceMode::DomainN1N1; return true; }
  return false;
}

Outcome<void> validate(const CapacityPolicy& policy, const Limits& limits) {
  if (!policy.id.valid()) {
    return Error(ErrorCode::InvalidArgument, "policy identity is empty");
  }
  if (!policy.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "policy generation is not established", policy.id.view());
  }
  if (policy.headroom_floor_ppm > 1000000U) {
    return Error(ErrorCode::InvalidArgument, "protected headroom exceeds one hundred percent",
                 policy.id.view());
  }
  if (policy.headroom_floor_absolute.units > limits.max_capacity_units) {
    return Error(ErrorCode::LimitExceeded, "protected headroom exceeds the configured maximum",
                 policy.id.view());
  }
  CFN_RETURN_IF_ERROR(validate(policy.provenance));
  return Outcome<void>();
}

}  // namespace cfn
