// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef CONFIGURATION_MANAGER_HPP_
#define CONFIGURATION_MANAGER_HPP_

#include <expected>
#include <filesystem>
#include <optional>

#include "astl/astl_telemetry.h"
#include "config/astl_configuration.hpp"

namespace astl {
namespace ConfigurationManager {

/**
 * @brief Get the path to the .so / .dll file for the ASTL library
 *
 * For a statically linked Linux executable, returns the equivalent library path
 * derived from the executable location so callers can find the adjacent config directory.
 *
 * @return If successful, returns the path to the ASTL shared object file or its static-build equivalent.
 * If unsuccessful, returns an appropriate astl_status_code error.
 */
auto GetAstlFilePath() -> std::expected<std::filesystem::path, astl_status_code>;

/**
 * @brief Determine the path to the configuration JSON file and parse it into an AstlConfiguration object
 *
 * @return If successful, returns the path the configuration JSON file.
 * If unsuccessful, returns an appropriate astl_status_code error.
 */
auto GetConfiguration() -> std::expected<AstlConfiguration, astl_status_code>;

/**
 * @brief Set an in-process override for the ASTL load file path.
 *
 * When an override path is set, subsequent Orchestrator constructions will use
 * the specified .astl file as the source of persisted state instead of any
 * default or auto-detected location. Passing std::nullopt clears any
 * previously configured override and restores the default load behavior.
 *
 * @param load_file_path Optional filesystem path to a .astl file to use as
 *        the explicit load target. Use std::nullopt to remove the override.
 *
 * @note Thread-safety: This function is not guaranteed to be thread-safe.
 *       If multiple threads may set or clear the override concurrently, the
 *       caller must provide external synchronization.
 */
auto SetLoadFilePathOverride(const std::optional<std::filesystem::path>& load_file_path) -> void;

/**
 * @brief Get the current in-process override for the ASTL load file path.
 *
 * @return The currently configured override path if one has been set via
 *         SetLoadFilePathOverride; otherwise, std::nullopt to indicate that
 *         the default load behavior will be used.
 *
 * @note Thread-safety: This function is not guaranteed to be thread-safe.
 *       If multiple threads may read or modify the override concurrently,
 *       the caller must ensure appropriate synchronization around both
 *       setter and getter calls.
 */
auto GetLoadFilePathOverride() -> std::optional<std::filesystem::path>;

}  // namespace ConfigurationManager

}  // namespace astl

#endif  // CONFIGURATION_MANAGER_HPP_
