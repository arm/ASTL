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
