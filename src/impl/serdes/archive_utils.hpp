// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ARCHIVE_UTILS_HPP_
#define ARCHIVE_UTILS_HPP_

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "astl/astl_errors.h"

namespace astl::mz {
namespace fs = std::filesystem;

// TODO(ASTL-284): Determine optimal compression level and flags
// See levels in miniz.h: MZ_NO_COMPRESSION (0) to MZ_BEST_COMPRESSION (9)
constexpr uint32_t kZipCompressionLevel = 6;

/*
 * @Brief Zips the contents of the source directory into a zip file at the specified path.
 */
auto ZipDirectory(const fs::path& src_dir, const fs::path& zip_path) -> astl_status_code;

/*
 * @Brief Unzips the contents of the zip file into the specified destination directory.
 */
auto UnzipDirectory(const fs::path& zip_path, const fs::path& dst_dir) -> astl_status_code;

/*
 * @Brief Lists all entries in the specified zip file.
 */
auto ListEntries(const fs::path& zip_path) -> std::expected<std::vector<std::string>, astl_status_code>;

}  // namespace astl::mz

#endif  // ARCHIVE_UTILS_HPP_
