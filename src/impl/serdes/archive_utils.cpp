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

#include "archive_utils.hpp"

#include <miniz/miniz.h>

#include <expected>
#include <filesystem>

#include "astl/astl_errors.h"
#include "astl_logger.hpp"

namespace astl::mz {

namespace fs = std::filesystem;

auto ZipDirectory(const fs::path& src_dir, const fs::path& zip_path) -> astl_status_code {
  if (!fs::exists(src_dir) || !fs::is_directory(src_dir)) {
    ASTL_LOG_ERROR("zip_directory: source directory does not exist: {}", src_dir.string());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  mz_zip_archive zip{};
  if (mz_zip_writer_init_file(std::addressof(zip), zip_path.string().c_str(), 0) == MZ_FALSE) {
    ASTL_LOG_ERROR("zip_directory: mz_zip_writer_init_file failed: {}", zip_path.string());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  for (const auto& entry : fs::recursive_directory_iterator(src_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto rel         = fs::relative(entry.path(), src_dir);
    auto name_in_zip = rel.generic_string();

    ASTL_LOG_DEBUG("zip_directory: adding {}", name_in_zip);

    if (mz_zip_writer_add_file(std::addressof(zip), name_in_zip.c_str(), entry.path().string().c_str(), nullptr, 0,
                               kZipCompressionLevel) == MZ_FALSE) {
      ASTL_LOG_ERROR("zip_directory: failed to add file {}", entry.path().string());
      mz_zip_writer_end(std::addressof(zip));
      return ASTL_STATUS_INTERNAL_ERROR;
    }
  }

  if (!mz_zip_writer_finalize_archive(std::addressof(zip))) {
    ASTL_LOG_ERROR("zip_directory: mz_zip_writer_finalize_archive failed");
    mz_zip_writer_end(std::addressof(zip));
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  mz_zip_writer_end(std::addressof(zip));
  ASTL_LOG_DEBUG("zip_directory: successfully created {}", zip_path.string());
  return ASTL_STATUS_SUCCESS;
}

auto UnzipDirectory(const fs::path& zip_path, const fs::path& dst_dir) -> astl_status_code {
  if (!fs::exists(zip_path)) {
    ASTL_LOG_ERROR("unzip_directory: zip file does not exist: {}", zip_path.string());
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  fs::create_directories(dst_dir);

  mz_zip_archive zip{};
  if (mz_zip_reader_init_file(std::addressof(zip), zip_path.string().c_str(), 0) == MZ_FALSE) {
    ASTL_LOG_ERROR("unzip_directory: mz_zip_reader_init_file failed: {}", zip_path.string());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto num_files = mz_zip_reader_get_num_files(std::addressof(zip));
  ASTL_LOG_DEBUG("unzip_directory: extracting {} entries", num_files);

  for (mz_uint i = 0; i < num_files; ++i) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(std::addressof(zip), i, std::addressof(stat))) {
      ASTL_LOG_ERROR("unzip_directory: file_stat failed at index {}", i);
      mz_zip_reader_end(std::addressof(zip));
      return ASTL_STATUS_INTERNAL_ERROR;
    }

    auto out_path = dst_dir / fs::path(stat.m_filename);

    if (stat.m_is_directory) {
      fs::create_directories(out_path);
      continue;
    }

    fs::create_directories(out_path.parent_path());

    ASTL_LOG_DEBUG("unzip_directory: extracting {}", out_path.string());

    if (mz_zip_reader_extract_to_file(std::addressof(zip), i, out_path.string().c_str(), 0) == MZ_FALSE) {
      ASTL_LOG_ERROR("unzip_directory: failed to extract {}", out_path.string());
      mz_zip_reader_end(std::addressof(zip));
      return ASTL_STATUS_INTERNAL_ERROR;
    }
  }

  mz_zip_reader_end(std::addressof(zip));
  ASTL_LOG_DEBUG("unzip_directory: successfully extracted to {}", dst_dir.string());
  return ASTL_STATUS_SUCCESS;
}

auto ListEntries(const fs::path& zip_path) -> std::expected<std::vector<std::string>, astl_status_code> {
  std::vector<std::string> out_entries{};

  if (!fs::exists(zip_path)) {
    ASTL_LOG_ERROR("list_entries: zip file does not exist: {}", zip_path.string());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }

  mz_zip_archive zip{};
  if (mz_zip_reader_init_file(std::addressof(zip), zip_path.string().c_str(), 0) == MZ_FALSE) {
    ASTL_LOG_ERROR("list_entries: mz_zip_reader_init_file failed: {}", zip_path.string());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto num_files = mz_zip_reader_get_num_files(std::addressof(zip));
  ASTL_LOG_DEBUG("list_entries: zip contains {} entries", num_files);

  for (mz_uint i = 0; i < num_files; ++i) {
    mz_zip_archive_file_stat stat{};
    if (mz_zip_reader_file_stat(std::addressof(zip), i, std::addressof(stat))) {
      out_entries.emplace_back(stat.m_filename);
    }
  }

  mz_zip_reader_end(std::addressof(zip));
  return out_entries;
}

}  // namespace astl::mz
