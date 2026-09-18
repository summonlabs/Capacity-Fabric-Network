// Capacity Fabric Network - strict text parsing and formatting.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Parsing is strict: no leading or trailing whitespace, no signs where a
// magnitude is expected, no overflow, no partial consumption. A parse either
// yields the exact value or fails.
#ifndef CFN_CORE_TEXT_HPP
#define CFN_CORE_TEXT_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cfn/core/api.hpp"

namespace cfn::text {

[[nodiscard]] CFN_API bool parse_u64(std::string_view text, std::uint64_t& out) noexcept;
[[nodiscard]] CFN_API bool parse_i64(std::string_view text, std::int64_t& out) noexcept;
[[nodiscard]] CFN_API bool parse_u32(std::string_view text, std::uint32_t& out) noexcept;
[[nodiscard]] CFN_API bool parse_u16(std::string_view text, std::uint16_t& out) noexcept;
[[nodiscard]] CFN_API bool parse_bool(std::string_view text, bool& out) noexcept;
/// Parses a percentage such as "12.5" or "12.5%" into parts per million.
[[nodiscard]] CFN_API bool parse_percent_ppm(std::string_view text, std::uint32_t& out) noexcept;

[[nodiscard]] CFN_API std::string format_u64(std::uint64_t value);
[[nodiscard]] CFN_API std::string format_i64(std::int64_t value);
/// Formats parts per million as a percentage with at most six decimals.
[[nodiscard]] CFN_API std::string format_ppm(std::uint32_t ppm);

[[nodiscard]] CFN_API std::string_view trim(std::string_view text) noexcept;
[[nodiscard]] CFN_API bool iequals(std::string_view lhs, std::string_view rhs) noexcept;
[[nodiscard]] CFN_API std::vector<std::string_view> split(std::string_view text, char delimiter);
[[nodiscard]] CFN_API std::string join(const std::vector<std::string>& parts, std::string_view separator);
[[nodiscard]] CFN_API bool starts_with(std::string_view text, std::string_view prefix) noexcept;

}  // namespace cfn::text

#endif  // CFN_CORE_TEXT_HPP
