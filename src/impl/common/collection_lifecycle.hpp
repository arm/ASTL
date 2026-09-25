// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
#ifndef COLLECTION_LIFECYCLE_HPP_
#define COLLECTION_LIFECYCLE_HPP_
#include "astl/astl_errors.h"
namespace astl {
enum class CollectionLifecycleState { UNCONFIGURED, CONFIGURED, STARTING, STARTED, PAUSED, STOPPED };
enum class CollectionLifecycleAction { START, PAUSE, RESUME, STOP };
[[nodiscard]] auto CheckCollectionLifecycleAction(CollectionLifecycleState state, CollectionLifecycleAction action)
    -> astl_status_code;
}  // namespace astl
#endif  // COLLECTION_LIFECYCLE_HPP_
