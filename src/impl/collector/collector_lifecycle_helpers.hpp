// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef COLLECTOR_LIFECYCLE_HELPERS_HPP_
#define COLLECTOR_LIFECYCLE_HELPERS_HPP_

#include <utility>

#include "astl_logger.hpp"
#include "common/collection_lifecycle.hpp"

namespace astl::collector_detail {

// Callers hold the collection mutex. Marker emission remains collector-specific.
// Preserve the existing failure semantics: a sink error does not undo the sampler/state change.
template <typename SamplerT, typename EmitMarkerFn>
auto PauseCollectionLifecycle(CollectionLifecycleState& state, SamplerT* sampler, EmitMarkerFn&& emit_marker)
    -> astl_status_code {
  const auto status = CheckCollectionLifecycleAction(state, CollectionLifecycleAction::PAUSE);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (sampler) {
    sampler->Pause();
  } else {
    ASTL_LOG_WARNING("PauseCollection called when no periodic sampler initialized");
  }
  state = CollectionLifecycleState::PAUSED;
  return std::forward<EmitMarkerFn>(emit_marker)();
}

template <typename SamplerT, typename EmitMarkerFn>
auto ResumeCollectionLifecycle(CollectionLifecycleState& state, SamplerT* sampler, EmitMarkerFn&& emit_marker)
    -> astl_status_code {
  const auto status = CheckCollectionLifecycleAction(state, CollectionLifecycleAction::RESUME);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  // Emit the resume marker before allowing new samples to be produced.
  const auto emit_status = std::forward<EmitMarkerFn>(emit_marker)();
  if (sampler) {
    sampler->Resume();
  } else {
    ASTL_LOG_WARNING("ResumeCollection called when no periodic sampler initialized");
  }
  state = CollectionLifecycleState::STARTED;
  return emit_status;
}

}  // namespace astl::collector_detail

#endif  // COLLECTOR_LIFECYCLE_HELPERS_HPP_
