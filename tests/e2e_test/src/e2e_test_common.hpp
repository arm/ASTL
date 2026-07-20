// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file e2e_test_common.hpp
 * @brief Common definitions and utilities for E2E tests
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"

namespace astl_test {

/**
 * @brief Helper for synchronizing multiple threads in phases
 *
 * Provides phase-based synchronization where threads can:
 * - Wait for a specific phase to be reached
 * - Advance to the next phase
 * - Report errors to stop all threads
 */
struct ThreadSyncHelper {
  std::mutex              mutex;
  std::condition_variable cv;
  std::atomic<int>        phase{0};
  std::atomic<bool>       error_occurred{false};

  /**
   * @brief Wait until the specified phase is reached or an error occurs
   * @param expected_phase The phase number to wait for
   */
  void WaitForPhase(int expected_phase) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this, expected_phase] { return phase >= expected_phase || error_occurred.load(); });
  }

  /**
   * @brief Advance to the next phase and notify waiting threads
   * @param next_phase The phase number to advance to
   */
  void AdvanceToPhase(int next_phase) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      phase = next_phase;
    }
    cv.notify_all();
  }

  /**
   * @brief Report an error and unblock all waiting threads
   */
  void ReportError() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      error_occurred = true;
    }
    cv.notify_all();
  }
};

/**
 * @brief Check if MockScmi is accessible at the given path
 * @param sysfs_root Path to the MockScmi mount point
 * @return true if accessible, false otherwise
 */
auto CheckMockScmi(const std::string& sysfs_root) -> bool;

/**
 * @brief Get a target by name from ASTL
 * @param target_name Name of the target to find
 * @param target_properties Output parameter for target properties
 * @return true if target found, false otherwise
 */
auto GetTargetByName(const std::string& target_name, astl_target_props_t& target_properties) -> bool;

/**
 * @brief Get available metrics for a target
 * @param target_handle Handle to the target
 * @param metric_handles Output vector of metric handles
 * @param metric_names Output vector of metric names
 * @return true if metrics found, false otherwise
 */
auto GetMetricsOnTarget(astl_target_handle_t target_handle, std::vector<astl_metric_handle_t>& metric_handles,
                        std::vector<std::string>& metric_names) -> bool;

/**
 * @brief Retrieve and count collected samples for all metrics
 * @param target_handle Handle to the target
 * @param metric_handles Vector of metric handles to check
 * @param metric_names Vector of metric names (for display)
 * @return Total number of samples collected across all metrics
 */
auto RetrieveSamples(astl_target_handle_t target_handle, const std::vector<astl_metric_handle_t>& metric_handles,
                     const std::vector<std::string>& metric_names) -> uint32_t;

}  // namespace astl_test
