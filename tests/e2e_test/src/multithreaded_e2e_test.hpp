// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file multithreaded_e2e_test.hpp
 * @brief Header for multi-threaded E2E test
 */

#pragma once

#include "e2e_test_common.hpp"

namespace astl_test {

/**
 * @brief Test ASTL API calls from multiple threads
 *
 * This test validates that ASTL APIs work correctly when called from
 * different threads. Specifically:
 * - Thread 1: Configures collection parameters
 * - Thread 2: Starts collection
 * - Thread 3: Collects samples and stops collection
 *
 * The test initializes its own configuration from environment variables.
 */
void TestMultiThreadedEndToEnd();

/**
 * @brief Test multiple threads attempting to configure after collection starts
 *
 * This test validates error handling when multiple threads try to configure
 * the same target after one thread has already started collection:
 * - Thread 1: Configures and starts collection
 * - Thread 2-4: Attempt to configure (should fail with ASTL_STATUS_TARGET_ACTIVE)
 * - Thread 5: Stops collection
 */
void TestMultipleConfigureAfterStart();

}  // namespace astl_test
