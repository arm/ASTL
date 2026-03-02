/*
 * SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file astl.h
 * @brief Umbrella header for the Arm SoC Telemetry Library public C API.
 *
 * Including this header brings in the core telemetry APIs (`astl_telemetry.h`),
 * error/status codes (`astl_errors.h`) and version query helpers (`astl_version.h`).
 * Prefer including only the specific headers you need in translation units that
 * are sensitive to compile time; otherwise this convenience header is fine.
 */
#ifndef INCLUDE_ASTL_H_
#define INCLUDE_ASTL_H_

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl/astl_version.h"

#endif  // INCLUDE_ASTL_H_
