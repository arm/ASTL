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

#ifndef FILESYSTEM_PROCESS_LOCK_HPP_
#define FILESYSTEM_PROCESS_LOCK_HPP_

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__linux__)
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

#include "astl/astl_errors.h"
#include "astl_file_interface.hpp"
#include "astl_logger.hpp"

namespace astl {

namespace process_lock_detail {

/**
 * @brief True when FileSystemT is the concrete ASTL FileInterface type.
 *
 * For non-FileInterface backends (for example mock file systems in unit tests),
 * FilesystemProcessLock intentionally behaves as a no-op.
 */
template <typename FileSystemT>
inline constexpr bool kRealFilesystem = std::is_same_v<std::remove_cvref_t<FileSystemT>, FileInterface>;

inline constexpr unsigned int kSharedLockFileMode = 0777U;

}  // namespace process_lock_detail

/**
 * @brief RAII wrapper for a process-scoped filesystem lock.
 *
 * On Linux with FileInterface, this acquires an advisory non-blocking exclusive
 * lock via `flock(LOCK_EX | LOCK_NB)` on a lock file under the interface's base
 * path and releases it in the destructor.
 *
 * For non-FileInterface types (for example mocks), lock operations are no-ops
 * that report success so existing tests do not require host filesystem locking.
 */
template <typename FileSystemT>
class FilesystemProcessLock {
 public:
  /**
   * @brief Construct and attempt lock acquisition.
   * @param file_system File interface used to resolve lock root (ignored if lock_file_path is absolute).
   * @param lock_file_path Full path to lock file, or relative path resolved under file_system's base path.
   */
  explicit FilesystemProcessLock(FileSystemT& file_system, std::string_view lock_file_path)
      : _lock_file_name{lock_file_path} {
    const auto path = std::filesystem::path(lock_file_path);
    _root_to_lock   = path.is_absolute() ? path.parent_path() : ResolveRoot(file_system);
    if (!path.is_absolute()) {
      _lock_file_name = path.filename().string();
    }
    Acquire();
  }

  FilesystemProcessLock(const FilesystemProcessLock&)            = delete;
  FilesystemProcessLock& operator=(const FilesystemProcessLock&) = delete;

  FilesystemProcessLock(FilesystemProcessLock&& other) noexcept { *this = std::move(other); }
  FilesystemProcessLock& operator=(FilesystemProcessLock&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Release();
    _root_to_lock   = std::move(other._root_to_lock);
    _lock_file_name = std::move(other._lock_file_name);
    _status         = other._status;
    _lock_fd        = other._lock_fd;
    other._lock_fd  = -1;
    other._status   = ASTL_STATUS_SUCCESS;
    return *this;
  }

  ~FilesystemProcessLock() { Release(); }

  /** @brief Return acquisition outcome from constructor-triggered Acquire(). */
  [[nodiscard]] auto Status() const -> astl_status_code { return _status; }
  /** @brief Return true when a lock is currently held (or lock is a successful no-op backend). */
  [[nodiscard]] auto IsLocked() const -> bool { return _lock_fd >= 0 || IsNoopLock(); }

  /**
   * @brief Release the held lock (idempotent).
   *
   * Safe to call multiple times; only the first effective call unlocks/closes.
   */
  auto Release() noexcept -> void {
    if constexpr (!process_lock_detail::kRealFilesystem<FileSystemT>) {
      return;
    }

#if defined(__linux__)
    if (_lock_fd < 0) {
      return;
    }

    (void)::flock(_lock_fd, LOCK_UN);
    (void)::close(_lock_fd);
    _lock_fd = -1;
#endif
  }

 private:
  /** @brief Resolve lock root from file system backend. */
  [[nodiscard]] static auto ResolveRoot(FileSystemT& file_system) -> std::filesystem::path {
    if constexpr (process_lock_detail::kRealFilesystem<FileSystemT>) {
      return file_system.GetBasePath();
    }
    (void)file_system;
    return {};
  }

  /** @brief Indicate successful lock state for non-locking backends. */
  [[nodiscard]] auto IsNoopLock() const -> bool {
    if constexpr (process_lock_detail::kRealFilesystem<FileSystemT>) {
#if defined(__linux__)
      return false;
#else
      return _status == ASTL_STATUS_SUCCESS;
#endif
    } else {
      return _status == ASTL_STATUS_SUCCESS;
    }
  }

  /** @brief Backend-specific lock acquisition entry point. */
  auto Acquire() -> void {
    if constexpr (!process_lock_detail::kRealFilesystem<FileSystemT>) {
      _status = ASTL_STATUS_SUCCESS;
      return;
    } else {
#if !defined(__linux__)
      _status = ASTL_STATUS_SUCCESS;
      return;
#else
      const auto lock_file_path = _root_to_lock / _lock_file_name;
      // Create/read-write a shared lock file usable across users.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      const int fd = ::open(lock_file_path.c_str(), O_CREAT | O_RDWR, process_lock_detail::kSharedLockFileMode);
      if (fd < 0) {
        ASTL_LOG_ERROR("Failed to open process lock file '{}': {}", lock_file_path.string(), std::strerror(errno));
        _status = ASTL_STATUS_FILE_OPEN_FAILED;
        return;
      }
      // Keep permissions permissive even if the file already existed with tighter mode.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      if (::fchmod(fd, process_lock_detail::kSharedLockFileMode) != 0) {
        ASTL_LOG_WARNING("Failed to set permissions on process lock file '{}': {}", lock_file_path.string(),
                         std::strerror(errno));
      }

      if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int lock_errno = errno;
        (void)::close(fd);
        if (lock_errno == EWOULDBLOCK || lock_errno == EAGAIN) {
          _status = ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
          return;
        }
        ASTL_LOG_ERROR("Failed to lock process lock file '{}': {}", lock_file_path.string(), std::strerror(lock_errno));
        _status = ASTL_STATUS_FILE_OPEN_FAILED;
        return;
      }

      _lock_fd = fd;
      _status  = ASTL_STATUS_SUCCESS;
#endif
    }
  }

  std::filesystem::path _root_to_lock;
  std::string           _lock_file_name;               //!< Lock filename relative to _root_to_lock.
  astl_status_code      _status{ASTL_STATUS_SUCCESS};  //!< Final acquisition status.
  int                   _lock_fd{-1};                  //!< Linux file descriptor used for flock ownership.
};

}  // namespace astl

#endif  // FILESYSTEM_PROCESS_LOCK_HPP_
