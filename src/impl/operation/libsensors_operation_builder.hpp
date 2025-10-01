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
