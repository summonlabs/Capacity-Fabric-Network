// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/model/capacity_model.hpp"

namespace cfn {

Outcome<void> validate(const CapacityModel& model, const Limits& limits) {
  if (!model.id.valid()) {
    return Error(ErrorCode::InvalidArgument, "capacity model identity is empty");
  }
  if (!model.generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "capacity model generation is not established",
                 model.id.view());
  }
  if (!model.policy.valid()) {
    return Error(ErrorCode::InvalidArgument, "capacity model names no policy", model.id.view());
  }
  if (!model.policy_generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "capacity model policy generation is not established",
                 model.id.view());
  }
  if (model.demand_shape.valid() && !model.demand_shape_generation.valid()) {
    return Error(ErrorCode::InvalidArgument, "capacity model demand shape generation is not established",
                 model.id.view());
  }
  if (!model.has_demand_shape() && model.demand_shape_generation.valid()) {
    return Error(ErrorCode::Contradictory,
                 "capacity model declares a demand shape generation without a demand shape",
                 model.id.view());
  }
  if (model.label.size() > model.label.capacity) {
    return Error(ErrorCode::LimitExceeded, "capacity model label exceeds its bound", model.id.view());
  }
  (void)limits;
  CFN_RETURN_IF_ERROR(validate(model.provenance));
  return Outcome<void>();
}

}  // namespace cfn
