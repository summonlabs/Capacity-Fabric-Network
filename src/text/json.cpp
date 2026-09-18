// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/text/json.hpp"

#include <cstdio>

namespace cfn::text {

std::string json_escape(std::string_view text) {
  std::string result;
  result.reserve(text.size() + 8U);
  for (const char character : text) {
    const unsigned char byte = static_cast<unsigned char>(character);
    switch (character) {
      case '"': result.append("\\\""); break;
      case '\\': result.append("\\\\"); break;
      case '\b': result.append("\\b"); break;
      case '\f': result.append("\\f"); break;
      case '\n': result.append("\\n"); break;
      case '\r': result.append("\\r"); break;
      case '\t': result.append("\\t"); break;
      default:
        if (byte < 0x20U) {
          char buffer[8] = {};
          const int written = std::snprintf(buffer, sizeof(buffer), "\\u%04x", byte);
          if (written > 0) {
            result.append(buffer, static_cast<std::size_t>(written));
          }
        } else {
          result.push_back(character);
        }
        break;
    }
  }
  return result;
}

JsonWriter::JsonWriter(bool pretty) : pretty_(pretty) {}

void JsonWriter::indent() {
  if (!pretty_) {
    return;
  }
  out_.push_back('\n');
  for (int index = 0; index < depth_; ++index) {
    out_.append("  ");
  }
}

void JsonWriter::separate() {
  if (first_.empty()) {
    return;
  }
  if (!first_.back()) {
    out_.push_back(',');
  }
  first_.back() = false;
  indent();
}

void JsonWriter::begin_value() {
  // A value that follows a key is written directly; a value in an array or an
  // object without a key needs its own separator.
  if (pending_value_) {
    pending_value_ = false;
    return;
  }
  separate();
}

void JsonWriter::begin_object() {
  begin_value();
  out_.push_back('{');
  first_.push_back(true);
  ++depth_;
}

void JsonWriter::end_object() {
  pending_value_ = false;
  --depth_;
  if (!first_.empty()) {
    const bool was_empty = first_.back();
    first_.pop_back();
    if (!was_empty) {
      indent();
    }
  }
  out_.push_back('}');
}

void JsonWriter::begin_array() {
  begin_value();
  out_.push_back('[');
  first_.push_back(true);
  ++depth_;
}

void JsonWriter::end_array() {
  pending_value_ = false;
  --depth_;
  if (!first_.empty()) {
    const bool was_empty = first_.back();
    first_.pop_back();
    if (!was_empty) {
      indent();
    }
  }
  out_.push_back(']');
}

void JsonWriter::key(std::string_view name) {
  if (!first_.empty()) {
    if (!first_.back()) {
      out_.push_back(',');
    }
    first_.back() = false;
    indent();
  }
  out_.push_back('"');
  out_.append(json_escape(name));
  out_.append("\":");
  if (pretty_) {
    out_.push_back(' ');
  }
  pending_value_ = true;
}

void JsonWriter::value_string(std::string_view value) {
  begin_value();
  out_.push_back('"');
  out_.append(json_escape(value));
  out_.push_back('"');
}

void JsonWriter::value_u64(std::uint64_t value) { begin_value(); out_.append(std::to_string(value)); }
void JsonWriter::value_i64(std::int64_t value) { begin_value(); out_.append(std::to_string(value)); }
void JsonWriter::value_bool(bool value) { begin_value(); out_.append(value ? "true" : "false"); }
void JsonWriter::value_null() { begin_value(); out_.append("null"); }

void JsonWriter::field(std::string_view name, std::string_view value) {
  key(name);
  value_string(value);
}

void JsonWriter::field(std::string_view name, std::uint64_t value) {
  key(name);
  value_u64(value);
}

void JsonWriter::field(std::string_view name, std::int64_t value) {
  key(name);
  value_i64(value);
}

void JsonWriter::field(std::string_view name, bool value) {
  key(name);
  value_bool(value);
}

std::string JsonWriter::take() {
  std::string result = std::move(out_);
  out_.clear();
  first_.clear();
  pending_value_ = false;
  depth_ = 0;
  return result;
}

}  // namespace cfn::text