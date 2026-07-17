// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file multithreaded_e2e_test.cpp
 * @brief E2E test for ASTL multi-threaded API calls with MockScmi
 */

#include "multithreaded_e2e_test.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"

namespace astl_test {

/**
 * @brief Test: Multi-threaded phased execution of ASTL operations
 *
 * This test validates ASTL's thread safety by executing configure, start, and stop
 * operations from separate threads in a coordinated sequence.
 *
 * Test Flow:
 * 1. Thread 1: Configures metric collection parameters on target "scmi-mockscmi-tlm-0"
 * 2. Thread 2: Waits for configure to complete, then starts collection
 * 3. Thread 3: Waits for start to complete, collects data for 300ms, then stops
 * 4. Main thread: Retrieves and validates collected samples
 *
 * Success Criteria:
 * - All threads complete their operations successfully without errors
 * - Operations execute in the correct order (configure → start → stop)
 * - Sample collection yields non-zero results
 *
 * This is a nominal/positive test case validating the standard workflow.
 */
void TestMultiThreadedEndToEnd() {
  INFO("Multi-Threaded E2E with MockScmi");

  astl_target_props_t target_properties{};
  REQUIRE(GetTargetByName("scmi-mockscmi-tlm-0", target_properties));

  std::vector<astl_metric_handle_t> metric_handles;
  std::vector<std::string>          metric_names;
  REQUIRE(GetMetricsOnTarget(target_properties.handle, metric_handles, metric_names));

  ThreadSyncHelper  sync;
  std::atomic<bool> test_failed{false};

  // Thread 1: Configure collection parameters
  std::thread configure_thread([&sync, &test_failed, target_handle = target_properties.handle, &metric_handles]() {
    std::cout << "[Thread 1] Configuring..." << std::endl;

    astl_collection_params_t params{
        .size  = sizeof(astl_collection_params_t),
        .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD,

        .sampling_interval = 100,

        .collection_mode = ASTL_COLLECTION_MODE_SAMPLING,
    };
    ASTL_INIT_STRUCT(astl_configure_metric_collection_on_target_params_t, configure_params, .flags = 0,
                     .target_handle = target_handle, .collection_params = &params,
                     .metric_handles = metric_handles.data(),
                     .metric_count   = static_cast<uint32_t>(metric_handles.size()));
    auto status = astlConfigureMetricCollectionOnTarget(&configure_params);

    if (status != ASTL_STATUS_SUCCESS) {
      std::cerr << "[Thread 1] Configure failed: " << astlStatusString(status) << std::endl;
      test_failed = true;
      sync.ReportError();
      return;
    }

    std::cout << "[Thread 1] ✓ Configured" << std::endl;
    sync.AdvanceToPhase(1);
  });

  // Thread 2: Start collection
  std::thread start_thread([&sync, &test_failed, target_handle = target_properties.handle]() {
    sync.WaitForPhase(1);
    if (sync.error_occurred) {
      return;
    }

    std::cout << "[Thread 2] Starting..." << std::endl;
    ASTL_INIT_STRUCT(astl_start_collection_on_target_params_t, start_params, .flags = 0,
                     .target_handle = target_handle);
    auto status = astlStartCollectionOnTarget(&start_params);

    if (status != ASTL_STATUS_SUCCESS) {
      std::cerr << "[Thread 2] Start failed: " << astlStatusString(status) << std::endl;
      test_failed = true;
      sync.ReportError();
      return;
    }

    std::cout << "[Thread 2] ✓ Started" << std::endl;
    sync.AdvanceToPhase(2);
  });

  // Thread 3: Collect samples then stop
  std::thread stop_thread([&sync, &test_failed, target_handle = target_properties.handle]() {
    sync.WaitForPhase(2);
    if (sync.error_occurred) {
      return;
    }

    std::cout << "[Thread 3] Collecting for 300ms..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::cout << "[Thread 3] Stopping..." << std::endl;
    ASTL_INIT_STRUCT(astl_stop_collection_on_target_params_t, stop_params, .flags = 0, .target_handle = target_handle);
    auto status = astlStopCollectionOnTarget(&stop_params);

    if (status != ASTL_STATUS_SUCCESS) {
      std::cerr << "[Thread 3] Stop failed: " << astlStatusString(status) << std::endl;
      test_failed = true;
      sync.ReportError();
      return;
    }

    std::cout << "[Thread 3] ✓ Stopped" << std::endl;
    sync.AdvanceToPhase(3);
  });

  configure_thread.join();
  start_thread.join();
  stop_thread.join();

  REQUIRE_FALSE(test_failed.load());
  REQUIRE(sync.phase == 3);
  REQUIRE_FALSE(sync.error_occurred.load());

  std::cout << "\n=== Retrieving Samples ===" << std::endl;
  uint32_t samples_collected = RetrieveSamples(target_properties.handle, metric_handles, metric_names);

  std::cout << "\n✓ Total: " << samples_collected << " samples" << std::endl;
  REQUIRE(samples_collected > 0);
}

/**
 * @brief Test: Multiple concurrent configure attempts after collection has started
 *
 * This test validates that ASTL correctly rejects configuration changes once
 * collection has already started, even when multiple threads attempt it concurrently.
 *
 * Test Flow:
 * 1. Thread 1: Configures metrics and starts collection
 * 2. Threads 2-4: Simultaneously attempt to reconfigure after collection started
 * 3. Thread 5: Waits for all configure attempts, then stops collection
 *
 * Success Criteria:
 * - Initial configure and start operations succeed
 * - All 3 concurrent reconfigure attempts fail (expected behavior)
 * - No thread incorrectly succeeds at reconfiguring active collection
 * - Stop operation completes successfully
 *
 * This is a negative test case validating proper rejection of invalid configure attempts.
 */
void TestMultipleConfigureAfterStart() {
  INFO("Multiple Configure After Start Collection");

  astl_target_props_t target_properties{};
  REQUIRE(GetTargetByName("scmi-mockscmi-tlm-0", target_properties));

  std::vector<astl_metric_handle_t> metric_handles;
  std::vector<std::string>          metric_names;
  REQUIRE(GetMetricsOnTarget(target_properties.handle, metric_handles, metric_names));

  ThreadSyncHelper  sync;
  std::atomic<int>  failed_configs{0};
  std::atomic<int>  expected_failures{0};
  std::atomic<bool> test_failed{false};
  constexpr int     num_config_threads = 3;

  // Thread 1: Configure and start collection
  std::thread main_thread([&sync, &test_failed, target_handle = target_properties.handle, &metric_handles]() {
    std::cout << "[Thread 1] Configuring..." << std::endl;

    astl_collection_params_t params{
        .size  = sizeof(astl_collection_params_t),
        .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD,

        .sampling_interval = 100,

        .collection_mode = ASTL_COLLECTION_MODE_SAMPLING,
    };
    ASTL_INIT_STRUCT(astl_configure_metric_collection_on_target_params_t, configure_params, .flags = 0,
                     .target_handle = target_handle, .collection_params = &params,
                     .metric_handles = metric_handles.data(),
                     .metric_count   = static_cast<uint32_t>(metric_handles.size()));
    auto status = astlConfigureMetricCollectionOnTarget(&configure_params);

    if (status != ASTL_STATUS_SUCCESS) {
      std::cerr << "[Thread 1] Configure failed: " << astlStatusString(status) << std::endl;
      test_failed = true;
      sync.ReportError();
      return;
    }

    std::cout << "[Thread 1] ✓ Configured" << std::endl;
    std::cout << "[Thread 1] Starting collection..." << std::endl;

    ASTL_INIT_STRUCT(astl_start_collection_on_target_params_t, start_params, .flags = 0,
                     .target_handle = target_handle);
    status = astlStartCollectionOnTarget(&start_params);
    if (status != ASTL_STATUS_SUCCESS) {
      std::cerr << "[Thread 1] Start failed: " << astlStatusString(status) << std::endl;
      test_failed = true;
      sync.ReportError();
      return;
    }

    std::cout << "[Thread 1] ✓ Started collection" << std::endl;
    sync.AdvanceToPhase(1);
  });

  // Threads 2-4: Attempt to configure after collection has started
  std::vector<std::thread> config_threads;
  for (int i = 0; i < num_config_threads; ++i) {
    config_threads.emplace_back([&sync, &failed_configs, &expected_failures, target_handle = target_properties.handle,
                                 &metric_handles, thread_id = i + 2]() {
      sync.WaitForPhase(1);
      if (sync.error_occurred) {
        return;
      }

      std::cout << "[Thread " << thread_id << "] Attempting to configure after start..." << std::endl;

      astl_collection_params_t params{
          .size  = sizeof(astl_collection_params_t),
          .flags = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY,

          .sampling_interval = 200,

          .collection_mode = ASTL_COLLECTION_MODE_SAMPLING,
      };
      ASTL_INIT_STRUCT(astl_configure_metric_collection_on_target_params_t, configure_params, .flags = 0,
                       .target_handle = target_handle, .collection_params = &params,
                       .metric_handles = metric_handles.data(),
                       .metric_count   = static_cast<uint32_t>(metric_handles.size()));
      auto status = astlConfigureMetricCollectionOnTarget(&configure_params);

      if (status != ASTL_STATUS_SUCCESS) {
        std::cout << "[Thread " << thread_id << "] ✓ Configure correctly failed: " << astlStatusString(status)
                  << std::endl;
        expected_failures++;
      } else {
        std::cerr << "[Thread " << thread_id << "] ✗ Configure should have failed but succeeded!" << std::endl;
        failed_configs++;
      }
    });
  }

  // Thread 5: Stop collection after all configure attempts
  std::thread stop_thread([&sync, &test_failed, target_handle = target_properties.handle]() {
    sync.WaitForPhase(1);
    if (sync.error_occurred) {
      return;
    }

    // Wait a bit to ensure all configure threads have attempted
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "[Thread " << (num_config_threads + 2) << "] Stopping collection..." << std::endl;
    ASTL_INIT_STRUCT(astl_stop_collection_on_target_params_t, stop_params, .flags = 0, .target_handle = target_handle);
    auto status = astlStopCollectionOnTarget(&stop_params);

    if (status != ASTL_STATUS_SUCCESS) {
      std::cerr << "[Thread " << (num_config_threads + 2) << "] Stop failed: " << astlStatusString(status) << std::endl;
      test_failed = true;
      sync.ReportError();
      return;
    }

    std::cout << "[Thread " << (num_config_threads + 2) << "] ✓ Stopped collection" << std::endl;
    sync.AdvanceToPhase(2);
  });

  main_thread.join();
  for (auto& thread : config_threads) {
    thread.join();
  }
  stop_thread.join();

  REQUIRE_FALSE(test_failed.load());
  REQUIRE(sync.phase == 2);
  REQUIRE_FALSE(sync.error_occurred.load());

  // Validate that all configure attempts failed as expected
  CHECK(failed_configs.load() == 0);
  REQUIRE(expected_failures.load() == num_config_threads);

  std::cout << "\n✓ All " << num_config_threads << " configure attempts correctly failed" << std::endl;
}

}  // namespace astl_test
