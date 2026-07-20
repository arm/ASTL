/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file astl_test_hooks.h
 * @brief Test support hooks for the Arm SoC Telemetry Library.
 *
 * These symbols are intended for internal / test harness use and should not be
 * relied upon by production applications. They allow injecting a test
 * orchestrator implementation so that deterministic scenarios can be driven
 * during unit / integration testing.
 */
#ifndef INCLUDE_ASTL_TEST_HOOKS_H_
#define INCLUDE_ASTL_TEST_HOOKS_H_

#include "astl/astl_errors.h"
#include "astl/astl_utils.h"

/**
 * @brief Opaque handle type used by tests to represent a test orchestrator instance.
 */
typedef void* astl_test_orchestrator_t;

/**
 * @brief Inject (or swap) the active test orchestrator implementation.
 *
 * Provides a mechanism for tests to replace the library's internal test
 * orchestrator with a custom instance (e.g. a mock) while retrieving the
 * previous one so it can be restored after the test.
 *
 * Thread-safety: Should be called when no concurrent telemetry operations are
 * in flight. Intended strictly for test code.
 *
 * @param[in]  new_orchestrator       The replacement orchestrator instance.
 * @param[out] original_orchestrator  Receives the previously registered orchestrator
 *                                    (NULL if none). Must not be NULL.
 *
 * @return ASTL_STATUS_SUCCESS on success or an error status code on failure.
 */
ASTL_API astl_status_code astlInjectTestOrchestrator(astl_test_orchestrator_t  new_orchestrator,
                                                     astl_test_orchestrator_t* original_orchestrator) ASTL_API_NOEXCEPT;

#endif  // INCLUDE_ASTL_TEST_HOOKS_H_
