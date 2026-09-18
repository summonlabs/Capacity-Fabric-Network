// Capacity Fabric Network - bounded value containers.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CORE_BOUNDED_HPP
#define CFN_CORE_BOUNDED_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace cfn {

/// Fixed capacity, non-allocating, NUL terminated text.
template <std::size_t Capacity>
class BoundedString {
 public:
  static constexpr std::size_t capacity = Capacity;

  constexpr BoundedString() noexcept = default;

  [[nodiscard]] static constexpr std::optional<BoundedString> from(std::string_view text) noexcept {
    if (text.size() > Capacity) {
      return std::nullopt;
    }
    BoundedString result;
    result.assign_unchecked(text);
    return result;
  }

  [[nodiscard]] static constexpr BoundedString truncated(std::string_view text) noexcept {
    BoundedString result;
    result.assign_unchecked(text.size() > Capacity ? text.substr(0, Capacity) : text);
    return result;
  }

  [[nodiscard]] constexpr bool assign(std::string_view text) noexcept {
    if (text.size() > Capacity) {
      return false;
    }
    assign_unchecked(text);
    return true;
  }

  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return std::string_view(bytes_.data(), size_);
  }
  [[nodiscard]] constexpr const char* c_str() const noexcept { return bytes_.data(); }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  constexpr void clear() noexcept {
    bytes_.fill('\0');
    size_ = 0;
  }

  friend constexpr bool operator==(const BoundedString& lhs, const BoundedString& rhs) noexcept {
    return lhs.view() == rhs.view();
  }
  friend constexpr bool operator!=(const BoundedString& lhs, const BoundedString& rhs) noexcept {
    return !(lhs == rhs);
  }
  friend constexpr bool operator<(const BoundedString& lhs, const BoundedString& rhs) noexcept {
    return lhs.view() < rhs.view();
  }

 private:
  constexpr void assign_unchecked(std::string_view text) noexcept {
    bytes_.fill('\0');
    for (std::size_t index = 0; index < text.size(); ++index) {
      bytes_[index] = text[index];
    }
    size_ = static_cast<std::uint32_t>(text.size());
  }

  std::array<char, Capacity + 1> bytes_{};
  std::uint32_t size_ = 0;
};

/// Fixed capacity list. Growth beyond Capacity fails loudly instead of
/// silently allocating without limit.
template <class T>
class BoundedList {
 public:
  explicit BoundedList(std::size_t capacity) : capacity_(capacity) {}

  [[nodiscard]] bool try_push_back(const T& value) {
    if (items_.size() >= capacity_) {
      return false;
    }
    items_.push_back(value);
    return true;
  }

  [[nodiscard]] bool try_push_back(T&& value) {
    if (items_.size() >= capacity_) {
      return false;
    }
    items_.push_back(std::move(value));
    return true;
  }

  [[nodiscard]] bool at_capacity() const noexcept { return items_.size() >= capacity_; }
  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
  [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  void clear() noexcept { items_.clear(); }
  void reserve_exact() { items_.reserve(capacity_); }

  typename std::vector<T>::iterator begin() noexcept { return items_.begin(); }
  typename std::vector<T>::iterator end() noexcept { return items_.end(); }
  typename std::vector<T>::const_iterator begin() const noexcept { return items_.begin(); }
  typename std::vector<T>::const_iterator end() const noexcept { return items_.end(); }
  typename std::vector<T>::const_iterator cbegin() const noexcept { return items_.cbegin(); }
  typename std::vector<T>::const_iterator cend() const noexcept { return items_.cend(); }

  T& operator[](std::size_t index) noexcept { return items_[index]; }
  const T& operator[](std::size_t index) const noexcept { return items_[index]; }

  [[nodiscard]] const std::vector<T>& items() const noexcept { return items_; }
  [[nodiscard]] std::vector<T>& items() noexcept { return items_; }

 private:
  std::vector<T> items_;
  std::size_t capacity_;
};

}  // namespace cfn

#endif  // CFN_CORE_BOUNDED_HPP
