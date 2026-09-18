// Capacity Fabric Network - cooperative cancellation.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Cancellation is cooperative and checked at bounded intervals. Work that
// observes a cancelled token returns ErrorCode::Cancelled and publishes
// nothing: no partial snapshot is ever exposed as a result.
#ifndef CFN_CORE_CANCEL_HPP
#define CFN_CORE_CANCEL_HPP

#include <atomic>
#include <memory>

#include "cfn/core/api.hpp"

namespace cfn {

class CFN_API CancellationToken {
 public:
  CancellationToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

  [[nodiscard]] bool cancelled() const noexcept { return flag_->load(std::memory_order_acquire); }
  void cancel() noexcept { flag_->store(true, std::memory_order_release); }

  /// True when the token is usable, i.e. it owns a shared flag.
  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(flag_); }

  /// Stable identity of this token, used to track which work is in flight.
  [[nodiscard]] const void* identity() const noexcept { return flag_.get(); }

 private:
  std::shared_ptr<std::atomic<bool>> flag_;
};

}  // namespace cfn

#endif  // CFN_CORE_CANCEL_HPP