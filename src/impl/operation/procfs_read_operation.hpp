// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_READ_OPERATION_HPP_
#define PROCFS_READ_OPERATION_HPP_

#include "common/procfs_utils.hpp"
#include "operation/operation.hpp"

namespace astl {

/**
 * @brief Operation describing a single procfs field read.
 */
struct ProcfsReadOperation : public Operation {
  procfs::FieldDescriptor field_descriptor;

  ProcfsReadOperation() = delete;
  explicit ProcfsReadOperation(procfs::FieldDescriptor field_descriptor)
      : field_descriptor(std::move(field_descriptor)) {}
};

}  // namespace astl

#endif  // PROCFS_READ_OPERATION_HPP_
