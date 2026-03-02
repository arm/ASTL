// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef I_LIBSENSORS_OPERATION_BUILDER_HPP_
#define I_LIBSENSORS_OPERATION_BUILDER_HPP_

#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include <sensors/sensors.h>

#  include "operation/operation.hpp"
#  include "target.hpp"

namespace astl {

class LibsensorsOperationBuilder {
 public:
  LibsensorsOperationBuilder(const sensors_chip_name* chip, int subfeature_number);

  [[nodiscard]] auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code>;

 private:
  const sensors_chip_name* _chip;
  int                      _subfeature_number;
};

static_assert(OperationBuilder<LibsensorsOperationBuilder>,
              "LibsensorsOperationBuilder does not satisfy OperationBuilder concept");

}  // namespace astl

#endif  // defined(ASTL_INCLUDE_LIBSENSORS)

#endif  // I_LIBSENSORS_OPERATION_BUILDER_HPP_
