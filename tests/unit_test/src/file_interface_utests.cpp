#include <catch2/catch_test_macros.hpp>
#ifndef _WIN32
#  include <unistd.h>
#endif
#include <cstdlib>  // getenv

#include "astl_file_interface.hpp"

// Test for astl::FileInterface
// ScopedTestFile is a RAII helper class to create and delete test files.
// This helps to construct and destruct test files automatically, ensuring that
// the test environment is clean before and after each test section.
class ScopedTestFile {
 public:
  ScopedTestFile(const std::string &path, const std::string &content) : _filePath(path) {
    std::ofstream file(_filePath);
    if (file.is_open()) {
      file << content;
      file.close();
    } else {
      throw std::runtime_error("Failed to create test file: " + _filePath);
    }
  }

  ScopedTestFile(const ScopedTestFile &)            = default;
  ScopedTestFile &operator=(const ScopedTestFile &) = default;
  ScopedTestFile(ScopedTestFile &&)                 = default;
  ScopedTestFile &operator=(ScopedTestFile &&)      = default;

  ~ScopedTestFile() { std::remove(_filePath.c_str()); }

  const std::string &Path() const { return _filePath; }

 private:
  std::string _filePath;
};

#ifndef _WIN32
// RAII helper to switch EUID
struct ScopedDropRoot {
  uid_t old_euid;
  bool  did_drop = false;

  ScopedDropRoot() : old_euid{geteuid()} {
    if (old_euid != 0) {
      // not running as root
      return;
    }
    if (const auto *sudo_uid = std::getenv("SUDO_UID")) {
      uid_t unpriv = static_cast<uid_t>(std::stoi(sudo_uid));
      if (seteuid(unpriv) == 0) {
        did_drop = true;
      }
    }
  }

  ScopedDropRoot(const ScopedDropRoot &)            = delete;
  ScopedDropRoot &operator=(const ScopedDropRoot &) = delete;
  ScopedDropRoot(ScopedDropRoot &&)                 = delete;
  ScopedDropRoot &operator=(ScopedDropRoot &&)      = delete;

  ~ScopedDropRoot() {
    if (did_drop) {
      // restore root
      (void)seteuid(old_euid);
    }
  }
};
#endif

// Test the main functionality of FileInterface when used with full absolute file paths.
// Covers: read, write, IsValid, HasReadPermission, HasWritePermission.
TEST_CASE("FileInterface functionality with absolute path", "[file_interface]") {
  astl::FileInterface sysfs;
  const auto *const   valid_path = "/tmp/test_sysfs_valid";
  const auto *const   content    = "test";
  ScopedTestFile      file(valid_path, content);

  SECTION("IsValid() detects existing and non-existing files") {
    const auto *const invalid_path = "/tmp/test_sysfs_invalid";
    auto              result       = sysfs.IsValid(invalid_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == false);  // file doesn't exist yet
    result = sysfs.IsValid(valid_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == true);
  }

  SECTION("Read() test file read interface") {
    std::string output;
    REQUIRE(sysfs.Read(file.Path(), output) == ASTL_STATUS_SUCCESS);
    REQUIRE(output == content);
  }

  SECTION("Read() test file read interface error code") {
#ifndef _WIN32
    std::string    output;
    ScopedTestFile no_read_perm_file("/tmp/test_sysfs_no_read_perm", "data");
    std::filesystem::permissions(
        no_read_perm_file.Path(),
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::remove);
    ScopedDropRoot drop_root_perms;
    REQUIRE(sysfs.Read(no_read_perm_file.Path(), output) == ASTL_STATUS_FILE_OPEN_FAILED);
#endif
  }

  SECTION("Write() test file write interface") {
    std::string       output;
    const auto *const new_content = "new_test";
    sysfs.Write(file.Path(), new_content);
    REQUIRE(sysfs.Read(file.Path(), output) == ASTL_STATUS_SUCCESS);
    REQUIRE(output == new_content);
  }

  SECTION("Write() test file write interface error code") {
#ifndef _WIN32
    const auto *const new_content = "new_test";
    ScopedTestFile    no_write_perm_file("/tmp/test_sysfs_no_write_perm", "data");
    std::filesystem::permissions(no_write_perm_file.Path(),
                                 std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                     std::filesystem::perms::others_write,
                                 std::filesystem::perm_options::remove);
    ScopedDropRoot drop_sudo;
    REQUIRE(sysfs.Write(no_write_perm_file.Path(), new_content) == ASTL_STATUS_FILE_OPEN_FAILED);
#endif
  }

  SECTION("HasReadPermission() responds to permission changes") {
    ScopedTestFile read_perm_file("/tmp/test_sysfs_read_perm", "data");

    auto result = sysfs.HasReadPermission(read_perm_file.Path());
    REQUIRE(result.has_value());
    REQUIRE(result.value() == true);
    std::filesystem::permissions(
        read_perm_file.Path(),
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::remove);
    result = sysfs.HasReadPermission(read_perm_file.Path());
    REQUIRE(result.has_value());
    REQUIRE(result.value() == false);
  }

  SECTION("HasWritePermission() responds to permission changes") {
    ScopedTestFile write_perm_file("/tmp/test_sysfs_write_perm", "data");

    auto result = sysfs.HasWritePermission(write_perm_file.Path());
    REQUIRE(result.has_value());
    REQUIRE(result.value() == true);
    std::filesystem::permissions(write_perm_file.Path(),
                                 std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                     std::filesystem::perms::others_write,
                                 std::filesystem::perm_options::remove);
    result = sysfs.HasWritePermission(write_perm_file.Path());
    REQUIRE(result.has_value());
    REQUIRE(result.value() == false);
  }
}

// Test the main functionality of FileInterface when used with relative file paths.
// Covers: read, write, IsValid, HasReadPermission, HasWritePermission.
TEST_CASE("FileInterface functionality with relative path", "[file_interface]") {
  astl::FileInterface sysfs("/tmp");
  const auto *const   rel_path = "test_rel_sysfs_valid";
  const auto *const   abs_path = "/tmp/test_rel_sysfs_valid";
  const auto *const   content  = "test";
  ScopedTestFile      file(abs_path, content);

  SECTION("IsValid() detects existing and non-existing files") {
    const auto *const invalid_path = "test_sysfs_invalid";
    auto              result       = sysfs.IsValid(invalid_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == false);  // file doesn't exist yet
    result = sysfs.IsValid(rel_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == true);
  }

  SECTION("Read() test file read interface") {
    std::string output;
    REQUIRE(sysfs.Read(rel_path, output) == ASTL_STATUS_SUCCESS);
    REQUIRE(output == content);
  }

  SECTION("Write() test file write interface") {
    std::string       output;
    const auto *const new_content = "new_test";
    sysfs.Write(rel_path, new_content);
    REQUIRE(sysfs.Read(rel_path, output) == ASTL_STATUS_SUCCESS);
    REQUIRE(output == new_content);
  }

  SECTION("HasReadPermission() responds to permission changes") {
    ScopedTestFile    read_perm_file("/tmp/test_rel_sysfs_read_perm", "data");
    const auto *const relative_path = "test_rel_sysfs_read_perm";

    auto result = sysfs.HasReadPermission(relative_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == true);
    std::filesystem::permissions(
        read_perm_file.Path(),
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::remove);
    result = sysfs.HasReadPermission(relative_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == false);  // file doesn't exist yet
  }

  SECTION("HasWritePermission() responds to permission changes") {
    ScopedTestFile    write_perm_file("/tmp/test_rel_sysfs_write_perm", "data");
    const auto *const relative_path = "test_rel_sysfs_write_perm";

    auto result = sysfs.HasWritePermission(relative_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == true);
    std::filesystem::permissions(write_perm_file.Path(),
                                 std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                     std::filesystem::perms::others_write,
                                 std::filesystem::perm_options::remove);
    result = sysfs.HasWritePermission(relative_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == false);  // file doesn't exist yet
  }
}
