// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef IMMEDIATE_READ_OUTCOME_HPP_
#define IMMEDIATE_READ_OUTCOME_HPP_

#include "astl/astl_errors.h"
#include "common/i_raw_sample_sink.hpp"

namespace astl {
class CollectorManager;

namespace detail {
// Collector callbacks are synchronous. Thread-local scope excludes periodic reads on other threads.
struct ImmediateReadOutcome {
  inline static thread_local ImmediateReadOutcome* current_ = nullptr;
  const CollectorManager*                          manager;
  const ITarget*                                   target;
  bool                                             has_samples = false;
  EmptyResultReason                                reason      = EmptyResultReason::UNKNOWN;
  ImmediateReadOutcome*                            previous    = current_;

  ImmediateReadOutcome(const CollectorManager* owner, const ITarget* read_target)
      : manager(owner), target(read_target) {
    current_ = this;
  }
  ImmediateReadOutcome(const ImmediateReadOutcome&)                    = delete;
  auto operator=(const ImmediateReadOutcome&) -> ImmediateReadOutcome& = delete;
  ImmediateReadOutcome(ImmediateReadOutcome&&)                         = delete;
  auto operator=(ImmediateReadOutcome&&) -> ImmediateReadOutcome&      = delete;
  ~ImmediateReadOutcome() { current_ = previous; }

  static auto For(const CollectorManager* owner, const ITarget* read_target) -> ImmediateReadOutcome* {
    return current_ && current_->manager == owner && current_->target == read_target ? current_ : nullptr;
  }
  auto Status() const -> astl_status_code {
    if (has_samples) {
      return ASTL_STATUS_SUCCESS;
    }
    switch (reason) {
      case EmptyResultReason::SAMPLE_DROPPED:
        return ASTL_STATUS_SAMPLE_DROPPED;
      case EmptyResultReason::NO_NEW_SAMPLE:
        return ASTL_STATUS_NO_NEW_SAMPLE;
      default:
        return ASTL_STATUS_NO_DATA_COLLECTED;
    }
  }
};
}  // namespace detail
}  // namespace astl

#endif  // IMMEDIATE_READ_OUTCOME_HPP_
