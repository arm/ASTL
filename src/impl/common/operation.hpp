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

#ifndef OPERATION_HPP_
#define OPERATION_HPP_

#include <chrono>
#include <vector>

namespace astl {

using SamplingInterval = std::chrono::duration<uint32_t, std::milli>;

// base class for operations for collectors to perform to enable or sample metrics
struct Operation {
  virtual ~Operation() = default;

  Operation()                            = default;
  Operation(const Operation&)            = default;
  Operation& operator=(const Operation&) = default;
  Operation(Operation&&)                 = default;
  Operation& operator=(Operation&&)      = default;
};

using OperationSequence = std::vector<std::unique_ptr<Operation>>;

}  // namespace astl

#endif  // OPERATION_HPP_