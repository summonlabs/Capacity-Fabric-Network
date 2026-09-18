// Capacity Fabric Network - snapshot binding validation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A snapshot computed against one generation vector must never be presented as
// an answer for a different one. Validation is a pure comparison: it cannot be
// influenced by the caller beyond supplying the current generations.
#ifndef CFN_ENGINE_INVALIDATION_HPP
#define CFN_ENGINE_INVALIDATION_HPP

#include "cfn/core/outcome.hpp"
#include "cfn/engine/snapshot.hpp"

namespace cfn {

/// Classifies why the snapshot no longer applies. Returns StalenessReason::None
/// when every bound generation still matches.
[[nodiscard]] CFN_API StalenessReason classify_staleness(const CapacitySnapshot& snapshot,
                                                         const GenerationVector& current) noexcept;

/// Rejects a snapshot whose binding no longer matches the current generations.
/// The error code identifies the first mismatch in a fixed precedence order, so
/// the rejection is stable and explainable.
[[nodiscard]] CFN_API Outcome<void> validate_binding(const CapacitySnapshot& snapshot,
                                                     const GenerationVector& current);

}  // namespace cfn

#endif  // CFN_ENGINE_INVALIDATION_HPP
