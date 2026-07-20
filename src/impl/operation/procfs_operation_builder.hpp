// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_OPERATION_BUILDER_HPP_
#define PROCFS_OPERATION_BUILDER_HPP_

#include "common/procfs_utils.hpp"
#include "operation/operation.hpp"
#include "target.hpp"

namespace astl {

class ProcfsOperationBuilder {
 public:
  explicit ProcfsOperationBuilder(procfs::FieldDescriptor field_descriptor);

  [[nodiscard]] auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code>;

 private:
  procfs::FieldDescriptor _field_descriptor;
};

static_assert(OperationBuilder<ProcfsOperationBuilder>,
              "ProcfsOperationBuilder does not satisfy OperationBuilder concept");

}  // namespace astl

#endif  // PROCFS_OPERATION_BUILDER_HPP_
