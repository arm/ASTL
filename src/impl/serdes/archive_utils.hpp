/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

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
