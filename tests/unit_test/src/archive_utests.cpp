// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <string>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
#include "serdes/archive_utils.hpp"

namespace fs = std::filesystem;

static void WriteTextFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream ofs(path, std::ios::binary);
  REQUIRE(ofs.good());
  ofs << text;
  REQUIRE(ofs.good());
}

static std::string ReadTextFile(const fs::path& path) {
  std::ifstream ifs(path, std::ios::binary);
  REQUIRE(ifs.good());
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

TEST_CASE("archive_dir_zip::zip_directory round-trip", "[zip_cache]") {
  // Arrange: create a small directory tree
  const fs::path base = fs::temp_directory_path() / "astl_miniz_zip_test";
  const fs::path src  = base / "tmp";
  const fs::path dst  = base / "tmp_restored";
  const fs::path zip  = base / "cache.zip";

  fs::remove_all(base);
  fs::create_directories(src);

  TempFileGuard base_guard{base};

  WriteTextFile(src / "a.txt", "hello\n");
  WriteTextFile(src / "sub" / "b.txt", "world\n");

  REQUIRE(astl::mz::ZipDirectory(src, zip) == ASTL_STATUS_SUCCESS);
  REQUIRE(fs::exists(zip));

  std::vector<std::string> entries = astl::mz::ListEntries(zip).value();
  REQUIRE(entries.size() >= 2);

  REQUIRE(astl::mz::UnzipDirectory(zip, dst) == ASTL_STATUS_SUCCESS);

  // Assert: files exist and contents match
  REQUIRE(fs::exists(dst / "a.txt"));
  REQUIRE(fs::exists(dst / "sub" / "b.txt"));

  REQUIRE(ReadTextFile(dst / "a.txt") == "hello\n");
  REQUIRE(ReadTextFile(dst / "sub" / "b.txt") == "world\n");
}

TEST_CASE("archive_dir_zip::zip_directory empty dir", "[zip_cache]") {
  // Arrange: create an empty directory
  const fs::path base = fs::temp_directory_path() / "astl_miniz_zip_test_empty";
  const fs::path src  = base / "tmp_empty";
  const fs::path dst  = base / "tmp_empty_restored";
  const fs::path zip  = base / "cache_empty.zip";

  fs::remove_all(base);
  fs::create_directories(src);

  TempFileGuard base_guard{base};

  REQUIRE(astl::mz::ZipDirectory(src, zip) == ASTL_STATUS_SUCCESS);
  REQUIRE(fs::exists(zip));

  std::vector<std::string> entries = astl::mz::ListEntries(zip).value();
  REQUIRE(entries.empty());  // empty dir should yield no entries

  REQUIRE(astl::mz::UnzipDirectory(zip, dst) == ASTL_STATUS_SUCCESS);

  REQUIRE(fs::exists(dst));
  REQUIRE(fs::is_directory(dst));
}

TEST_CASE("archive_dir_zip::zip_directory non-existent dir", "[zip_cache]") {
  const fs::path base = fs::temp_directory_path() / "astl_miniz_zip_test_nonexistent";
  const fs::path src  = base / "tmp_nonexistent";
  const fs::path zip  = base / "cache_nonexistent.zip";

  fs::remove_all(base);

  TempFileGuard base_guard{base};

  REQUIRE(astl::mz::ZipDirectory(src, zip) != ASTL_STATUS_SUCCESS);
  REQUIRE(!fs::exists(zip));
}
