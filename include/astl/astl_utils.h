/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file astl_utils.h
 * @brief Utility macros and visibility decoration helpers for the public ASTL C API.
 *
 * Defines the `ASTL_API` macro which annotates exported functions for the
 * supported toolchains / platforms (dllexport/dllimport on Windows and default
 * ELF symbol visibility elsewhere).  For older compilers lacking visibility
 * controls the macro is defined empty.
 */
#ifndef ASTL_UTILS_H_
#define ASTL_UTILS_H_

// define a `ASTL_API` macro to define visibility for API functions
#ifdef _WIN32
#  ifdef ASTL_BUILD
// building the ASTL library, as opposed to including it
#    define ASTL_API __declspec(dllexport)
#  else
// using the ASTL API
#    define ASTL_API __declspec(dllimport)
#  endif
#else
#  if __GNUC__ >= 4
#    define ASTL_API __attribute__((visibility("default")))
#  else
// older compilers - fall back to empty macro def
#    define ASTL_API
#  endif
#endif

#ifdef __cplusplus
// if we're creating the C++ implementation of the API, declare API functions as noexcept
// (mainly so static analyzers will warn if the implementation has uncaught exceptions)
#  define ASTL_API_NOEXCEPT noexcept
#else
#  define ASTL_API_NOEXCEPT
#endif

#endif  // ASTL_UTILS_H_
