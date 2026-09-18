// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/core/text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>

#include "cfn/core/checked.hpp"

namespace cfn::text {

namespace {

[[nodiscard]] bool is_digit(char value) noexcept { return value >= '0' && value <= '9'; }

[[nodiscard]] char lower(char value) noexcept {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

}  // namespace

bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
  if (text.empty()) {
    return false;
  }
  std::uint64_t value = 0;
  for (const char character : text) {
    if (!is_digit(character)) {
      return false;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
    std::uint64_t scaled = 0;
    if (!checked::mul_u64(value, 10, scaled)) {
      return false;
    }
    std::uint64_t next = 0;
    if (!checked::add_u64(scaled, digit, next)) {
      return false;
    }
    value = next;
  }
  out = value;
  return true;
}

bool parse_i64(std::string_view text, std::int64_t& out) noexcept {
  if (text.empty()) {
    return false;
  }
  bool negative = false;
  if (text.front() == '-') {
    negative = true;
    text.remove_prefix(1);
  } else if (text.front() == '+') {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return false;
  }
  std::uint64_t magnitude = 0;
  if (!parse_u64(text, magnitude)) {
    return false;
  }
  const std::uint64_t limit = negative ? (1ULL << 63) : ((1ULL << 63) - 1ULL);
  if (magnitude > limit) {
    return false;
  }
  if (negative) {
    if (magnitude == (1ULL << 63)) {
      out = std::numeric_limits<std::int64_t>::min();
      return true;
    }
    out = -static_cast<std::int64_t>(magnitude);
    return true;
  }
  out = static_cast<std::int64_t>(magnitude);
  return true;
}

bool parse_u32(std::string_view text, std::uint32_t& out) noexcept {
  std::uint64_t value = 0;
  if (!parse_u64(text, value)) {
    return false;
  }
  return checked::narrow_u32(value, out);
}

bool parse_u16(std::string_view text, std::uint16_t& out) noexcept {
  std::uint64_t value = 0;
  if (!parse_u64(text, value)) {
    return false;
  }
  return checked::narrow_u16(value, out);
}

bool parse_bool(std::string_view text, bool& out) noexcept {
  if (iequals(text, "true") || text == "1") {
    out = true;
    return true;
  }
  if (iequals(text, "false") || text == "0") {
    out = false;
    return true;
  }
  return false;
}

bool parse_percent_ppm(std::string_view text, std::uint32_t& out) noexcept {
  if (!text.empty() && text.back() == '%') {
    text.remove_suffix(1);
  }
  if (text.empty()) {
    return false;
  }
  const std::size_t dot = text.find('.');
  std::string_view whole = dot == std::string_view::npos ? text : text.substr(0, dot);
  std::string_view fraction = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
  if (whole.empty()) {
    whole = "0";
  }
  if (fraction.size() > 4) {
    return false;
  }
  std::uint64_t whole_value = 0;
  if (!parse_u64(whole, whole_value)) {
    return false;
  }
  std::uint64_t fraction_value = 0;
  if (!fraction.empty() && !parse_u64(fraction, fraction_value)) {
    return false;
  }
  // Four fractional digits is exactly the parts-per-million resolution.
  for (std::size_t index = fraction.size(); index < 4; ++index) {
    fraction_value *= 10;
  }
  std::uint64_t total = 0;
  if (!checked::mul_u64(whole_value, 10000, total)) {
    return false;
  }
  std::uint64_t combined = 0;
  if (!checked::add_u64(total, fraction_value, combined)) {
    return false;
  }
  if (combined > 1000000ULL) {
    return false;
  }
  out = static_cast<std::uint32_t>(combined);
  return true;
}

std::string format_u64(std::uint64_t value) { return std::to_string(value); }
std::string format_i64(std::int64_t value) { return std::to_string(value); }

std::string format_ppm(std::uint32_t ppm) {
  const std::uint32_t whole = ppm / 10000u;
  const std::uint32_t fraction = ppm % 10000u;
  if (fraction == 0) {
    return std::to_string(whole) + "%";
  }
  char buffer[16] = {};
  const int written = std::snprintf(buffer, sizeof(buffer), "%u.%04u%%", whole, fraction);
  if (written <= 0) {
    return std::to_string(whole) + "%";
  }
  std::string result(buffer, static_cast<std::size_t>(written));
  while (!result.empty() && result.size() >= 2) {
    const std::size_t index = result.size() - 2;
    if (result[index] != '0') {
      break;
    }
    result.erase(index, 1);
  }
  return result;
}

std::string_view trim(std::string_view text) noexcept {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (lower(lhs[index]) != lower(rhs[index])) {
      return false;
    }
  }
  return true;
}

std::vector<std::string_view> split(std::string_view text, char delimiter) {
  std::vector<std::string_view> parts;
  if (text.empty()) {
    return parts;
  }
  std::size_t start = 0;
  while (true) {
    const std::size_t position = text.find(delimiter, start);
    if (position == std::string_view::npos) {
      parts.push_back(text.substr(start));
      break;
    }
    parts.push_back(text.substr(start, position - start));
    start = position + 1;
  }
  return parts;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
  std::string result;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (index != 0) {
      result.append(separator);
    }
    result.append(parts[index]);
  }
  return result;
}

bool starts_with(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

}  // namespace cfn::text