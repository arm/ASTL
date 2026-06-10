// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "astl/astl_test_hooks.h"

#include "astl_internal_status.hpp"
#include "orchestrator/orchestrator.hpp"

// Swaps out the current orchestrator's raw pointer and replaces it with a new raw pointer.
// For use only in tests, mostly tests covering the C -> C++ wrapper layer
ASTL_API auto astlInjectTestOrchestrator(astl_test_orchestrator_t  new_orchestrator,
                                         astl_test_orchestrator_t* original_orchestrator) noexcept -> astl_status_code {
  astl::ClearLastStatusString();
  if (original_orchestrator == nullptr) {
    astl::SetLastStatusFallback(ASTL_STATUS_BAD_ARGUMENT);
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  auto current_orchestrator = astl::Orchestrator::GetInstance();
  if (!current_orchestrator) {
    astl::MaybeSetLastStatusFallback(current_orchestrator.error());
    return astl::IsInternalStatus(current_orchestrator.error()) ? ASTL_STATUS_INTERNAL_ERROR
                                                                : current_orchestrator.error();
  }

  // caller of astlInjectTestOrchestrator is now responsible for managing the original orchestrator resources
  // most likely by swapping it back in when done
  *original_orchestrator = current_orchestrator.value().get().release();

  auto* raw_orchestrator_ptr = static_cast<astl::Orchestrator*>(new_orchestrator);
  auto  orchestrator         = astl::Orchestrator::GetInstance();
  if (!orchestrator) {
    astl::MaybeSetLastStatusFallback(orchestrator.error());
    return astl::IsInternalStatus(orchestrator.error()) ? ASTL_STATUS_INTERNAL_ERROR : orchestrator.error();
  }

  orchestrator.value().get().reset(raw_orchestrator_ptr);
  astl::ClearLastStatusString();
  return ASTL_STATUS_SUCCESS;
}
