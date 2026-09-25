// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "common/collection_lifecycle.hpp"

#include <array>
#include <cstddef>

namespace astl {
namespace {

// Columns follow CollectionLifecycleState: unconfigured, configured, starting, started, paused, stopped.
// Rows follow CollectionLifecycleAction: start, pause, resume, stop.
constexpr auto kLifecycleStateCount = static_cast<std::size_t>(CollectionLifecycleState::STOPPED) + 1;
using StatusRow                     = std::array<astl_status_code, kLifecycleStateCount>;
// Keep one state per line so rows can be compared with the enum order above.
// clang-format off
constexpr std::array kLifecycleStatuses{
    StatusRow{  // START
        ASTL_STATUS_COLLECTION_NOT_CONFIGURED,
        ASTL_STATUS_SUCCESS,
        ASTL_STATUS_COLLECTION_ALREADY_RUNNING,
        ASTL_STATUS_COLLECTION_ALREADY_RUNNING,
        ASTL_STATUS_INVALID_STATE_TRANSITION,
        ASTL_STATUS_INVALID_STATE_TRANSITION,
    },
    StatusRow{  // PAUSE
        ASTL_STATUS_COLLECTION_NOT_CONFIGURED,
        ASTL_STATUS_COLLECTION_NOT_RUNNING,
        ASTL_STATUS_COLLECTION_NOT_RUNNING,
        ASTL_STATUS_SUCCESS,
        ASTL_STATUS_COLLECTION_ALREADY_PAUSED,
        ASTL_STATUS_COLLECTION_ALREADY_STOPPED,
    },
    StatusRow{  // RESUME
        ASTL_STATUS_COLLECTION_NOT_CONFIGURED,
        ASTL_STATUS_COLLECTION_NOT_PAUSED,
        ASTL_STATUS_COLLECTION_ALREADY_RUNNING,
        ASTL_STATUS_COLLECTION_ALREADY_RUNNING,
        ASTL_STATUS_SUCCESS,
        ASTL_STATUS_COLLECTION_ALREADY_STOPPED,
    },
    StatusRow{  // STOP
        ASTL_STATUS_COLLECTION_NOT_CONFIGURED,
        ASTL_STATUS_COLLECTION_NOT_RUNNING,
        ASTL_STATUS_INVALID_STATE_TRANSITION,
        ASTL_STATUS_SUCCESS,
        ASTL_STATUS_SUCCESS,
        ASTL_STATUS_COLLECTION_ALREADY_STOPPED,
    },
};
// clang-format on

}  // namespace

auto CheckCollectionLifecycleAction(CollectionLifecycleState state, CollectionLifecycleAction action)
    -> astl_status_code {
  const auto state_index  = static_cast<std::size_t>(state);
  const auto action_index = static_cast<std::size_t>(action);
  auto       status       = ASTL_STATUS_INTERNAL_ERROR;
  if (state == CollectionLifecycleState::UNCONFIGURED) {
    // Preserve unconfigured precedence even for an unknown action.
    status = ASTL_STATUS_COLLECTION_NOT_CONFIGURED;
  } else if (action_index < kLifecycleStatuses.size() && state_index < kLifecycleStatuses.front().size()) {
    status = kLifecycleStatuses.at(action_index).at(state_index);
  }
  return status;
}

}  // namespace astl
