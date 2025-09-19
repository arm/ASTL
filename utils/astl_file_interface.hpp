#ifndef ASTL_FILE_INTERFACE_HPP
#define ASTL_FILE_INTERFACE_HPP

#include <expected>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "astl/astl_errors.h"
#include "astl_logger.hpp"

namespace astl {

/**
 * @brief The C++ interface representing a file interface
 */

class FileInterface {
 public:
  FileInterface() = default;

  /**
   * @brief Constructor that sets a base path.
   * All file operations will be resolved relative to this base path.
   * @param basePath The base directory for all file interactions.
   */
  explicit FileInterface(const std::filesystem::path &basePath) : _basePath(basePath) {}

  /**
   * @brief Check if the given file or directory exists.
   * @param path Relative or absolute path to check.
   * @return true if the file or directory exists, false otherwise.
   */
  std::expected<bool, astl_status_code> IsValid(const std::filesystem::path &path) const noexcept {
    try {
      return std::filesystem::exists(Resolve(path));
    } catch (const std::bad_alloc &e) {
      ASTL_LOG_ERROR("IsValid: {}", e.what());
      return std::unexpected(ASTL_STATUS_OUT_OF_MEMORY);
    } catch (const std::filesystem::filesystem_error &e) {
      ASTL_LOG_ERROR("IsValid: {}", e.what());
      return std::unexpected(ASTL_STATUS_FILE_ERROR);
    }
    // TODO(https://jira.arm.com/browse/ASTL-76) - Unhandled exception are caught at the top level.
  }

  auto GetSubdirectories() const -> std::expected<std::vector<std::filesystem::directory_entry>, astl_status_code> {
    try {
      std::vector<std::filesystem::directory_entry> entries;
      for (const auto &entry : std::filesystem::directory_iterator(Resolve(std::filesystem::path{}))) {
        if (entry.is_directory()) {
          entries.push_back(entry);
        }
      }
      return entries;
    } catch (const std::bad_alloc &e) {
      ASTL_LOG_ERROR("GetSubdirectories: {}", e.what());
      return std::unexpected(ASTL_STATUS_OUT_OF_MEMORY);
    } catch (const std::filesystem::filesystem_error &e) {
      ASTL_LOG_ERROR("GetSubdirectories: {}", e.what());
      return std::unexpected(ASTL_STATUS_FILE_ERROR);
    }
  }

  /**
   * @brief Check if the given file has read permissions.
   * @param path Relative or absolute path to the file.
   * @return true if readable by owner, group, or others; false otherwise.
   */
  std::expected<bool, astl_status_code> HasReadPermission(const std::filesystem::path &path) const noexcept {
    using perms = std::filesystem::perms;
    try {
      auto permissions = std::filesystem::status(Resolve(path)).permissions();
      return (permissions & (perms::owner_read | perms::group_read | perms::others_read)) != perms::none;
    } catch (const std::bad_alloc &e) {
      ASTL_LOG_ERROR("HasReadPermission: {}", e.what());
      return std::unexpected(ASTL_STATUS_OUT_OF_MEMORY);
    } catch (const std::filesystem::filesystem_error &e) {
      ASTL_LOG_ERROR("HasReadPermission: {}", e.what());
      return std::unexpected(ASTL_STATUS_FILE_ERROR);
    }
    // TODO(https://jira.arm.com/browse/ASTL-76) - Unhandled exception are caught at the top level.
  }

  /**
   * @brief Check if the given file has write permissions.
   * @param path Relative or absolute path to the file.
   * @return true if writable by owner, group, or others; false otherwise.
   */
  std::expected<bool, astl_status_code> HasWritePermission(const std::filesystem::path &path) const noexcept {
    using perms = std::filesystem::perms;
    try {
      auto permissions = std::filesystem::status(Resolve(path)).permissions();
      return (permissions & (perms::owner_write | perms::group_write | perms::others_write)) != perms::none;
    } catch (const std::bad_alloc &e) {
      ASTL_LOG_ERROR("HasWritePermission: {}", e.what());
      return std::unexpected(ASTL_STATUS_OUT_OF_MEMORY);
    } catch (const std::filesystem::filesystem_error &e) {
      ASTL_LOG_ERROR("HasWritePermission: {}", e.what());
      return std::unexpected(ASTL_STATUS_FILE_ERROR);
    }
    // TODO(https://jira.arm.com/browse/ASTL-76) - Unhandled exception are caught at the top level.
  }

  /**
   * @brief Read the entire contents of a file into a string.
   * @param path Relative or absolute path to the file.
   * @param opString Reference to a string where the file contents will be stored.
   * @return ASTL_STATUS_SUCCESS if the file is successfully read, or an error code otherwise.
   */
  astl_status_code Read(const std::filesystem::path &path, std::string &opString) const {
    std::ifstream file(Resolve(path));
    if (!file.is_open()) {
      return ASTL_STATUS_FILE_OPEN_FAILED;
    }
    opString.assign(std::istreambuf_iterator<char>(file), {});
    return ASTL_STATUS_SUCCESS;
  }

  /**
   * @brief Write a string value to a file.
   * @param path Relative or absolute path to the file.
   * @param value The string data to write into the file.
   * @return ASTL_STATUS_SUCCESS if the file is successfully written, or an error code otherwise.
   */
  astl_status_code Write(const std::filesystem::path &path, const std::string_view value) const {
    std::ofstream file(Resolve(path));
    if (!file.is_open()) {
      return ASTL_STATUS_FILE_OPEN_FAILED;
    }
    file << value;
    return ASTL_STATUS_SUCCESS;
  }

  const std::filesystem::path &GetBasePath() const { return _basePath; }

 private:
  // Resolve a given path relative to the base path if set.
  std::filesystem::path Resolve(const std::filesystem::path &path) const {
    if (_basePath.empty()) {
      return path;  // Allow it to behave as no base path
    }
    return _basePath / path;
  }

  std::filesystem::path _basePath;
};

}  // namespace astl

#endif  // ASTL_FILE_INTERFACE_HPP
