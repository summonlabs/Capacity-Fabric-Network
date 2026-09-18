// Capacity Fabric Network - bounded deterministic prediction.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Prediction is a separate, explicitly labelled product. It reads history and
// the current observed answer; it never writes either. Every prediction states
// its model, its assumptions, its evidence window, and a confidence derived
// from sample count, sample age, and extrapolation distance. When the evidence
// is insufficient the result is UNKNOWN - not a zero, and not a guess.
//
// All arithmetic is fixed point over integers so a prediction is reproducible
// bit for bit on every platform.
#ifndef CFN_ENGINE_PREDICTION_HPP
#define CFN_ENGINE_PREDICTION_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "cfn/core/limits.hpp"
#include "cfn/core/outcome.hpp"
#include "cfn/core/provenance.hpp"
#include "cfn/core/time.hpp"
#include "cfn/engine/snapshot.hpp"

namespace cfn {

enum class PredictionModelKind : std::uint8_t {
  None = 0,
  /// Repeat the most recent authoritative observation.
  LastValue = 1,
  /// Least squares linear fit with a clamped slope and a bounded horizon.
  BoundedLinearTrend = 2,
  /// Exponentially weighted level with a bounded smoothing factor.
  EwmaLevel = 3,
};

[[nodiscard]] CFN_API std::string_view to_string(PredictionModelKind kind) noexcept;
[[nodiscard]] CFN_API bool prediction_model_from_string(std::string_view text, PredictionModelKind& out) noexcept;

/// One authoritative observation of usable capacity.
struct CapacityObservation {
  ResourceId resource;
  Timestamp at;
  Capacity observed_usable;
  EvidenceClass evidence_class = EvidenceClass::Unknown;
  Provenance provenance;
};

struct PredictionRequest {
  /// Resource to predict. A null identity predicts the total across every
  /// resource that appears in the history.
  ResourceId resource;
  Timestamp horizon;
  PredictionModelKind kind = PredictionModelKind::BoundedLinearTrend;
  /// Minimum authoritative observations required before a prediction is made.
  std::uint32_t min_samples = 3;
  /// Oldest observation that may be used.
  Duration max_sample_age = Duration::from_hours(24);
  /// Largest distance between the newest sample and the horizon.
  Duration max_extrapolation = Duration::from_hours(24);
  /// Clamp on the fitted slope, in capacity units per hour.
  Capacity max_growth_per_hour = Capacity::from_units(1000000000ULL);
  /// Clamp on how far the prediction may move from the newest observation,
  /// expressed in parts per million of that observation.
  std::uint32_t max_extrapolation_ppm = 200000;
  /// Smoothing factor for EwmaLevel, in parts per million.
  std::uint32_t ewma_alpha_ppm = 250000;
  std::vector<CapacityObservation> history;
  FabricEpoch epoch;
  CapacitySnapshotId snapshot;
  Generation snapshot_generation;
};

struct ResourcePrediction {
  ResourceId resource;
  bool available = false;
  Capacity base_observed;
  Capacity predicted_available;
  /// Fitted trend in capacity units per hour, fixed point truncated.
  std::int64_t slope_per_hour = 0;
  std::uint32_t sample_count = 0;
  /// Distance from the newest sample to the horizon.
  Duration extrapolation{};
  std::uint32_t confidence_ppm = 0;
  bool slope_clamped = false;
  bool range_clamped = false;
};

struct PredictionResult {
  /// False means UNKNOWN: no prediction is available for this request.
  bool available = false;
  /// Always EvidenceClass::Predicted while available. A prediction is never an
  /// observation and can never be substituted for one.
  EvidenceClass evidence_class = EvidenceClass::Predicted;
  PredictionModelKind kind = PredictionModelKind::None;
  Capacity predicted_available;
  Capacity base_observed;
  std::uint32_t confidence_ppm = 0;
  Timestamp produced_at;
  Timestamp horizon;
  std::vector<ResourcePrediction> per_resource;
  std::vector<std::string> assumptions;
  std::string unavailable_reason;
  Provenance provenance;
  GenerationVector input_generations;

  /// Present for symmetry with the observation path and always false: a
  /// prediction may not be consumed as observed capacity.
  [[nodiscard]] constexpr bool usable_as_observation() const noexcept { return false; }
};

[[nodiscard]] CFN_API Outcome<PredictionResult> predict(const PredictionRequest& request, const Limits& limits,
                                                        Timestamp produced_at);

}  // namespace cfn

#endif  // CFN_ENGINE_PREDICTION_HPP
