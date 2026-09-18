// Capacity Fabric Network - structured explanation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// An explanation is a bounded list of labelled fields grouped into sections:
// raw capacity, every deduction, reserved, headroom, degraded, stranded with
// its cause, usable, predicted, bottlenecks, the generation vector, and
// provenance. Explanations are generated from the snapshot, never recorded
// alongside it, so they can never drift from the answer.
#ifndef CFN_EXPLAIN_EXPLANATION_HPP
#define CFN_EXPLAIN_EXPLANATION_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfn/core/error.hpp"
#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/engine/fragmentation.hpp"
#include "cfn/engine/prediction.hpp"
#include "cfn/engine/snapshot.hpp"

namespace cfn {

/// Maximum bytes retained per explanation value.
inline constexpr std::size_t kMaxExplanationTextBytes = 256;

struct ExplanationField {
  std::string section;
  std::string key;
  std::string value;
};

struct Explanation {
  std::string title;
  std::vector<ExplanationField> fields;

  [[nodiscard]] std::string to_text() const;
  [[nodiscard]] std::string to_json() const;
  [[nodiscard]] const std::string* find(std::string_view section, std::string_view key) const noexcept;
};

/// Explains a computed snapshot: accounting, deductions, fragmentation,
/// bottlenecks, generations, confidence, and closure.
[[nodiscard]] CFN_API Outcome<Explanation> explain(const CapacitySnapshot& snapshot, const Limits& limits);

/// Explains a prediction, including its model, assumptions, evidence window,
/// and confidence, and states explicitly that the result is not an observation.
[[nodiscard]] CFN_API Outcome<Explanation> explain(const PredictionResult& prediction, const Limits& limits);

/// Explains a standalone fit answer.
[[nodiscard]] CFN_API Outcome<Explanation> explain(const FitQuery& query, const FitQueryResult& result,
                                                   const Limits& limits);

/// Explains why a snapshot was rejected against current generations.
[[nodiscard]] CFN_API Outcome<Explanation> explain_rejection(const CapacitySnapshot& snapshot,
                                                             const GenerationVector& current,
                                                             const Error& error, const Limits& limits);

}  // namespace cfn

#endif  // CFN_EXPLAIN_EXPLANATION_HPP