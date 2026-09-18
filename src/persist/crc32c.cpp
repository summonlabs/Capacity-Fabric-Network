// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#include "cfn/persist/crc32c.hpp"

namespace cfn::crc32c {

namespace {

/// CRC32C uses the Castagnoli polynomial in reversed (reflected) form.
constexpr std::uint32_t kPolynomial = 0x82F63B78u;

/// A plain array rather than a std::array: the index is derived from data, and
/// a C array keeps the bounds obvious to both the compiler and static analysis.
struct Table {
  std::uint32_t values[256] = {};
};

[[nodiscard]] const std::uint32_t* table() {
  static const Table instance = [] {
    Table built;
    for (std::uint32_t index = 0; index < 256U; ++index) {
      std::uint32_t value = index;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value >> 1U) ^ (kPolynomial & (~(value & 1U) + 1U));
      }
      built.values[index] = value;
    }
    return built;
  }();
  return instance.values;
}

}  // namespace

std::uint32_t update(std::uint32_t state, const void* data, std::size_t size) noexcept {
  const std::uint32_t* lookup = table();
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t offset = 0; offset < size; ++offset) {
    const std::uint32_t slot = (state ^ static_cast<std::uint32_t>(bytes[offset])) & 0xFFU;
    state = lookup[static_cast<std::size_t>(slot)] ^ (state >> 8U);
  }
  return state;
}

std::uint32_t compute(const void* data, std::size_t size) noexcept {
  return update(kInitial, data, size) ^ 0xFFFFFFFFu;
}

std::uint32_t compute(std::span<const std::byte> data) noexcept {
  return compute(data.data(), data.size());
}

std::uint32_t compute(std::string_view data) noexcept {
  return compute(data.data(), data.size());
}

}  // namespace cfn::crc32c