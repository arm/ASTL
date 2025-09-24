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

#endif  // ASTL_UTILS_H_
