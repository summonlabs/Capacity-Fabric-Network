// Capacity Fabric Network - explicit result type.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
#ifndef CFN_CORE_OUTCOME_HPP
#define CFN_CORE_OUTCOME_HPP

#include <optional>
#include <type_traits>
#include <utility>

#include "cfn/core/error.hpp"

namespace cfn {

template <class T>
class Outcome {
 public:
  Outcome(T value) : value_(std::move(value)) {}
  Outcome(Error error) : error_(std::move(error)) {}

  bool has_value() const noexcept { return value_.has_value(); }
  explicit operator bool() const noexcept { return value_.has_value(); }

  T& value() & {
    require_value();
    return *value_;
  }
  const T& value() const& {
    require_value();
    return *value_;
  }
  T&& value() && {
    require_value();
    return std::move(*value_);
  }

  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  const Error& error() const noexcept { return error_; }
  ErrorCode code() const noexcept { return error_.code(); }

  T value_or(T fallback) const {
    return value_.has_value() ? *value_ : std::move(fallback);
  }

 private:
  void require_value() const {
    if (!value_.has_value()) {
      throw OutcomeAccessError(error_);
    }
  }

  std::optional<T> value_;
  Error error_{ErrorCode::Ok, "ok"};
};

template <>
class Outcome<void> {
 public:
  Outcome() = default;
  Outcome(Error error) : error_(std::move(error)) {}

  bool has_value() const noexcept { return error_.ok(); }
  explicit operator bool() const noexcept { return error_.ok(); }

  void value() const {
    if (!error_.ok()) {
      throw OutcomeAccessError(error_);
    }
  }

  const Error& error() const noexcept { return error_; }
  ErrorCode code() const noexcept { return error_.code(); }

 private:
  Error error_{ErrorCode::Ok, "ok"};
};

/// Propagates a rejection out of the current function unchanged.
#define CFN_RETURN_IF_ERROR(expr)     \
  do {                                \
    auto&& cfn_try_result = (expr);   \
    if (!cfn_try_result) {            \
      return cfn_try_result.error();  \
    }                                 \
  } while (false)

/// Propagates a rejection after attaching the subject it applies to.
#define CFN_RETURN_IF_ERROR_CTX(expr, context)         \
  do {                                                 \
    auto&& cfn_try_result = (expr);                    \
    if (!cfn_try_result) {                             \
      Error cfn_try_error = cfn_try_result.error();    \
      cfn_try_error.set_subject(context);              \
      return cfn_try_error;                            \
    }                                                  \
  } while (false)

}  // namespace cfn

#endif  // CFN_CORE_OUTCOME_HPP
