// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_TARGET_HPP_
#define PROCFS_TARGET_HPP_

#include <filesystem>
#include <string>
#include <utility>

#include "target.hpp"

namespace astl {

/**
 * @brief procfs-specific target carrying the root path used for discovery and collection.
 */
class ProcfsTarget : public Target {
 public:
  ProcfsTarget(std::string name, std::string description, std::filesystem::path procfs_root_path)
      : Target(std::move(name), std::move(description), CollectorType::PROCFS),
        _procfs_root_path(std::move(procfs_root_path)) {}

  [[nodiscard]] auto ProcfsRootPath() const -> const std::filesystem::path& { return _procfs_root_path; }

 private:
  std::filesystem::path _procfs_root_path;
};

}  // namespace astl

#endif  // PROCFS_TARGET_HPP_
