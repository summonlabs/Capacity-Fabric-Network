// Capacity Fabric Network - CRC32C integrity checking.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Every durable record and every transport frame carries a CRC32C
// (Castagnoli) checksum. It detects truncation, bit rot, and partial writes;
// it is an integrity check, not a security control.
#ifndef CFN_PERSIST_CRC32C_HPP
#define CFN_PERSIST_CRC32C_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cfn/core/api.hpp"

namespace cfn::crc32c {

inline constexpr std::uint32_t kInitial = 0xFFFFFFFFu;

[[nodiscard]] CFN_API std::uint32_t update(std::uint32_t state, const void* data, std::size_t size) noexcept;
[[nodiscard]] CFN_API std::uint32_t compute(const void* data, std::size_t size) noexcept;
[[nodiscard]] CFN_API std::uint32_t compute(std::span<const std::byte> data) noexcept;
[[nodiscard]] CFN_API std::uint32_t compute(std::string_view data) noexcept;

}  // namespace cfn::crc32c

#endif  // CFN_PERSIST_CRC32C_HPP
