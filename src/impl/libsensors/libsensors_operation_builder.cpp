// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "libsensors/libsensors_operation_builder.hpp"

#include "libsensors/libsensors_read_operation.hpp"
#include "operation/operation.hpp"
#include "target.hpp"

#if defined(ASTL_INCLUDE_LIBSENSORS)
namespace astl {

LibsensorsOperationBuilder::LibsensorsOperationBuilder(const sensors_chip_name* chip, int subfeature_number)
    : _chip(chip), _subfeature_number(subfeature_number) {}

[[nodiscard]] auto LibsensorsOperationBuilder::BuildOperations(const ITarget* target) const
    -> std::expected<OperationSequence, astl_status_code> {
  (void)target;
  OperationSequence seq;
  seq.push_back(std::make_unique<LibsensorsReadOperation>(_chip, _subfeature_number));
  return seq;
}

}  // namespace astl

#endif  // defined(ASTL_INCLUDE_LIBSENSORS)
