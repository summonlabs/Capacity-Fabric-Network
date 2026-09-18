// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "test_fixtures.hpp"
#include "test_support.hpp"

#include <cctype>
#include <string>

#include "cfn/cfn.hpp"

using namespace cfn;
using cfn::test::GraphFixture;
using cfn::test::TestContext;

namespace {

/// Minimal strict JSON well-formedness check. It exists so a malformed
/// rendering is a failing test rather than something a reader has to notice.
class JsonValidator {
 public:
  explicit JsonValidator(std::string_view text) : text_(text) {}

  [[nodiscard]] bool valid() {
    skip_space();
    if (!parse_value()) {
      return false;
    }
    skip_space();
    return position_ == text_.size();
  }

 private:
  void skip_space() {
    while (position_ < text_.size() &&
           (text_[position_] == ' ' || text_[position_] == '\n' || text_[position_] == '\r' ||
            text_[position_] == '\t')) {
      ++position_;
    }
  }

  [[nodiscard]] bool consume(char expected) {
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  [[nodiscard]] bool parse_string() {
    if (!consume('"')) {
      return false;
    }
    while (position_ < text_.size()) {
      const char value = text_[position_];
      if (value == '\\') {
        position_ += 2;
        continue;
      }
      if (value == '"') {
        ++position_;
        return true;
      }
      if (static_cast<unsigned char>(value) < 0x20U) {
        return false;
      }
      ++position_;
    }
    return false;
  }

  [[nodiscard]] bool parse_number() {
    const std::size_t begin = position_;
    while (position_ < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[position_])) != 0 ||
            text_[position_] == '-' || text_[position_] == '+' || text_[position_] == '.' ||
            text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
    }
    return position_ > begin;
  }

  [[nodiscard]] bool parse_literal(std::string_view word) {
    if (text_.compare(position_, word.size(), word) == 0) {
      position_ += word.size();
      return true;
    }
    return false;
  }

  [[nodiscard]] bool parse_value() {
    skip_space();
    if (position_ >= text_.size()) {
      return false;
    }
    const char value = text_[position_];
    if (value == '{') {
      ++position_;
      skip_space();
      if (consume('}')) {
        return true;
      }
      for (;;) {
        skip_space();
        if (!parse_string()) {
          return false;
        }
        skip_space();
        if (!consume(':')) {
          return false;
        }
        if (!parse_value()) {
          return false;
        }
        skip_space();
        if (consume('}')) {
          return true;
        }
        if (!consume(',')) {
          return false;
        }
      }
    }
    if (value == '[') {
      ++position_;
      skip_space();
      if (consume(']')) {
        return true;
      }
      for (;;) {
        if (!parse_value()) {
          return false;
        }
        skip_space();
        if (consume(']')) {
          return true;
        }
        if (!consume(',')) {
          return false;
        }
      }
    }
    if (value == '"') {
      return parse_string();
    }
    if (parse_literal("true") || parse_literal("false") || parse_literal("null")) {
      return true;
    }
    return parse_number();
  }

  std::string_view text_;
  std::size_t position_ = 0;
};

[[nodiscard]] bool json_well_formed(std::string_view text) {
  JsonValidator validator(text);
  return validator.valid();
}

[[nodiscard]] std::string value_of(const Explanation& explanation, const char* section,
                                   const char* key) {
  const std::string* found = explanation.find(section, key);
  return found == nullptr ? std::string("<missing>") : *found;
}

}  // namespace

CFN_TEST(explain, snapshot_explanation_covers_every_deduction) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.add_reservation("res", "cap-sa", 20);
  fixture.policy.headroom_floor_ppm = 10000;
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const auto explanation = explain(*snapshot, Limits());
  CFN_REQUIRE_OK(cfn_ctx, explanation);

  CFN_CHECK_EQ(value_of(*explanation, "accounting", "raw"), std::string("300"));
  CFN_CHECK_EQ(value_of(*explanation, "accounting", "reserved"), std::string("20"));
  CFN_CHECK_EQ(value_of(*explanation, "accounting", "protected_headroom"), std::string("2"));
  CFN_CHECK_EQ(value_of(*explanation, "accounting", "degraded"), std::string("0"));
  CFN_CHECK_EQ(value_of(*explanation, "accounting", "available"), std::string("278"));
  CFN_CHECK_EQ(value_of(*explanation, "accounting", "usable"), std::string("80"));
  CFN_CHECK_EQ(value_of(*explanation, "accounting", "stranded"), std::string("149"));
  CFN_CHECK_EQ(value_of(*explanation, "stranding", "bottleneck"), std::string("149"));
  CFN_CHECK_EQ(value_of(*explanation, "stranding", "segmentation"), std::string("0"));
  CFN_CHECK_EQ(value_of(*explanation, "closure", "verified"), std::string("true"));
  CFN_CHECK_EQ(value_of(*explanation, "generations", "fabric_epoch"), std::string("1"));
  CFN_CHECK(explanation->find("identity", "snapshot") != nullptr);
  CFN_CHECK(explanation->find("provenance", "observed_at") != nullptr);
}

CFN_TEST(explain, text_and_json_render) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const auto explanation = explain(*snapshot, Limits());
  CFN_REQUIRE_OK(cfn_ctx, explanation);
  const std::string rendered = explanation->to_text();
  CFN_CHECK(rendered.find("[accounting]") != std::string::npos);
  CFN_CHECK(rendered.find("raw") != std::string::npos);
  const std::string json = explanation->to_json();
  CFN_CHECK(json.find("\"accounting\"") != std::string::npos);
  CFN_CHECK(json.find("\"raw\"") != std::string::npos);
  CFN_CHECK_EQ(json.front(), '{');
  CFN_CHECK_EQ(json.back(), '}');
  CFN_CHECK(json_well_formed(json));
  CFN_CHECK(json.find("\"title\": ,") == std::string::npos);
}

CFN_TEST(explain, explanation_respects_its_entry_budget) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  Limits limits;
  limits.max_explanation_entries = 5;
  const auto explanation = explain(*snapshot, limits);
  CFN_REQUIRE_OK(cfn_ctx, explanation);
  CFN_CHECK(explanation->fields.size() <= 5U);
}

CFN_TEST(explain, zero_budget_is_rejected) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  Limits limits;
  limits.max_explanation_entries = 0;
  CFN_REQUIRE_ERROR(cfn_ctx, explain(*snapshot, limits), ErrorCode::LimitExceeded);
}

CFN_TEST(explain, every_rendering_is_well_formed_json) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  fixture.add_resource("orphan", 40, "fd-a");
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  const auto snapshot_explanation = explain(*snapshot, Limits());
  CFN_REQUIRE_OK(cfn_ctx, snapshot_explanation);
  CFN_CHECK(json_well_formed(snapshot_explanation->to_json()));

  std::vector<ResourceAccounting> rows = snapshot->per_resource;
  FitQuery query;
  query.source = *ResourceId::parse("src");
  query.sink = *ResourceId::parse("dst");
  query.magnitude = Capacity::from_units(40);
  const auto answer = fit_query(rows, fixture.topology, query, Limits());
  CFN_REQUIRE_OK(cfn_ctx, answer);
  const auto fit_explanation = explain(query, *answer, Limits());
  CFN_REQUIRE_OK(cfn_ctx, fit_explanation);
  CFN_CHECK(json_well_formed(fit_explanation->to_json()));

  PredictionRequest request;
  request.kind = PredictionModelKind::LastValue;
  request.min_samples = 1;
  CapacityObservation observation;
  observation.resource = *ResourceId::parse("res");
  observation.at = cfn::test::test_now();
  observation.observed_usable = Capacity::from_units(10);
  observation.evidence_class = EvidenceClass::Measured;
  observation.provenance = observed_provenance(cfn::test::test_source(),
                                               ProvenanceSource::TelemetryCollector,
                                               cfn::test::test_now(), Duration{});
  request.history.push_back(observation);
  const auto prediction = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, prediction);
  const auto prediction_explanation = explain(*prediction, Limits());
  CFN_REQUIRE_OK(cfn_ctx, prediction_explanation);
  CFN_CHECK(json_well_formed(prediction_explanation->to_json()));
}

CFN_TEST(explain, prediction_explanation_states_the_boundary) {
  PredictionRequest request;
  request.kind = PredictionModelKind::LastValue;
  request.history.push_back(*[] {
    CapacityObservation value;
    value.resource = *ResourceId::parse("res");
    value.at = cfn::test::test_now();
    value.observed_usable = Capacity::from_units(10);
    value.evidence_class = EvidenceClass::Measured;
    value.provenance = observed_provenance(cfn::test::test_source(),
                                           ProvenanceSource::TelemetryCollector,
                                           cfn::test::test_now(), Duration{});
    return std::optional<CapacityObservation>(value);
  }());
  const auto prediction = predict(request, Limits(), cfn::test::test_now());
  CFN_REQUIRE_OK(cfn_ctx, prediction);
  const auto explanation = explain(*prediction, Limits());
  CFN_REQUIRE_OK(cfn_ctx, explanation);
  CFN_CHECK_EQ(value_of(*explanation, "boundary", "is_observation"), std::string("false"));
  CFN_CHECK_EQ(value_of(*explanation, "boundary", "usable_as_observed_capacity"), std::string("false"));
  CFN_CHECK(explanation->find("assumptions", "assumption") != nullptr);
}

CFN_TEST(explain, fit_explanation_reports_the_query_and_the_answer) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 40);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  std::vector<ResourceAccounting> rows = snapshot->per_resource;
  FitQuery query;
  query.source = *ResourceId::parse("src");
  query.sink = *ResourceId::parse("dst");
  query.magnitude = Capacity::from_units(40);
  const auto answer = fit_query(rows, fixture.topology, query, Limits());
  CFN_REQUIRE_OK(cfn_ctx, answer);
  const auto explanation = explain(query, *answer, Limits());
  CFN_REQUIRE_OK(cfn_ctx, explanation);
  CFN_CHECK_EQ(value_of(*explanation, "result", "deliverable"), std::string("150"));
  CFN_CHECK_EQ(value_of(*explanation, "result", "satisfiable"), std::string("true"));
}

CFN_TEST(explain, rejection_explanation_names_the_error) {
  GraphFixture fixture = cfn::test::diamond(100, 50, 80);
  const auto snapshot = fixture.evaluate();
  CFN_REQUIRE_OK(cfn_ctx, snapshot);
  GenerationVector current = snapshot->generations;
  current.topology = Generation::from_value(4);
  const auto rejection = validate_binding(*snapshot, current);
  CFN_CHECK(!rejection);
  const auto explanation = explain_rejection(*snapshot, current, rejection.error(), Limits());
  CFN_REQUIRE_OK(cfn_ctx, explanation);
  CFN_CHECK_EQ(value_of(*explanation, "rejection", "code"), std::string("stale-topology"));
  CFN_CHECK_EQ(value_of(*explanation, "rejection", "staleness"), std::string("topology-changed"));
}