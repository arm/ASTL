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

#ifndef SENSORS_HPP_
#define SENSORS_HPP_

#include <sensors/sensors.h>

#include <utility>

#include "operation.hpp"

namespace astl {

struct LibsensorsReadOperation : public Operation {
  const sensors_chip_name* chip;
  int                      subfeature_number{0};

  LibsensorsReadOperation() = delete;
  LibsensorsReadOperation(const sensors_chip_name* chip, const int subfeature_number)
      : chip{chip}, subfeature_number{subfeature_number} {
    if (!chip) {
      throw std::invalid_argument("LibsensorsReadOperation requires non-null chip");
    }
  }
};

}  // namespace astl

#endif  // SENSORS_HPP_