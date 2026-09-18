// Capacity Fabric Network - deterministic JSON output.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Output only. The library never parses JSON; scenario input uses the strict
// line format in text/scenario.hpp.
#ifndef CFN_TEXT_JSON_HPP
#define CFN_TEXT_JSON_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfn/core/api.hpp"

namespace cfn::text {

[[nodiscard]] CFN_API std::string json_escape(std::string_view text);

class CFN_API JsonWriter {
 public:
  explicit JsonWriter(bool pretty = true);

  void begin_object();
  void end_object();
  void begin_array();
  void end_array();
  void key(std::string_view name);

  void value_string(std::string_view value);
  void value_u64(std::uint64_t value);
  void value_i64(std::int64_t value);
  void value_bool(bool value);
  void value_null();

  void field(std::string_view name, std::string_view value);
  void field(std::string_view name, std::uint64_t value);
  void field(std::string_view name, std::int64_t value);
  void field(std::string_view name, bool value);

  [[nodiscard]] const std::string& text() const noexcept { return out_; }
  [[nodiscard]] std::string take();

 private:
  /// Starts the next array element or member value.
  void begin_value();
  void separate();
  void indent();

  std::string out_;
  std::vector<bool> first_;
  bool pretty_ = true;
  bool pending_value_ = false;
  int depth_ = 0;
};

}  // namespace cfn::text

#endif  // CFN_TEXT_JSON_HPP