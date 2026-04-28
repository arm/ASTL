// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "operation/procfs_operation_builder.hpp"

#include "operation/procfs_read_operation.hpp"

namespace astl {

ProcfsOperationBuilder::ProcfsOperationBuilder(procfs::FieldDescriptor field_descriptor)
    : _field_descriptor(std::move(field_descriptor)) {}

auto ProcfsOperationBuilder::BuildOperations(const ITarget* target) const
    -> std::expected<OperationSequence, astl_status_code> {
  (void)target;
  OperationSequence sequence;
  sequence.push_back(std::make_unique<ProcfsReadOperation>(_field_descriptor));
  return sequence;
}

}  // namespace astl
