// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef LIFECYCLE_EVENT_HPP_
#define LIFECYCLE_EVENT_HPP_

#include <cstdint>
#include <string_view>

namespace astl {

/** Internal values emitted by the ASTL-generated lifecycle event metric. */
enum class LifecycleEventType : std::uint64_t {
  PAUSE      = 0,
  RESUME     = 1,
  CROP_BEGIN = 2,
  CROP_END   = 3,
};

inline constexpr std::string_view kLifecyclePauseEventName{"ASTL_LIFECYCLE_PAUSE"};
inline constexpr std::string_view kLifecycleResumeEventName{"ASTL_LIFECYCLE_RESUME"};
inline constexpr std::string_view kLifecycleCropBeginEventName{"ASTL_LIFECYCLE_CROP_BEGIN"};
inline constexpr std::string_view kLifecycleCropEndEventName{"ASTL_LIFECYCLE_CROP_END"};

/** Return whether an event name is reserved for ASTL's synthetic lifecycle metric. */
[[nodiscard]] constexpr auto IsReservedLifecycleEventName(std::string_view name) noexcept -> bool {
  return name == kLifecyclePauseEventName || name == kLifecycleResumeEventName ||
         name == kLifecycleCropBeginEventName || name == kLifecycleCropEndEventName;
}

}  // namespace astl

#endif  // LIFECYCLE_EVENT_HPP_
