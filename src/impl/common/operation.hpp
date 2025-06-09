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

#include <astl_logger.hpp>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

namespace astl {

using SamplingInterval = std::chrono::duration<uint32_t, std::milli>;
using SampleTimestamp  = std::chrono::time_point<std::chrono::steady_clock, std::chrono::microseconds>;

using OperationId                    = uint16_t;
constexpr size_t kOperationIdInvalid = std::numeric_limits<OperationId>::max();

// base class for operations for collectors to perform to enable or sample metrics
class Operation {
 public:
  virtual ~Operation() = default;

  Operation() : operation_id{GetNextOperationId()} {}
  Operation(const Operation&)            = default;
  Operation& operator=(const Operation&) = default;
  Operation(Operation&&)                 = default;
  Operation& operator=(Operation&&)      = default;

  /*
   * @brief Get an identifier associated with this Operation instance.
   * Useful for when a Collector needs to send SampledData back to Orchestrator and tie it to Metrics
   */
  OperationId GetId() const { return operation_id; }

 private:
  OperationId operation_id{kOperationIdInvalid};

  /*
   * @brief increment an operation base-class level identifier and return the current value
   * Since this is used in the constructor, this simply raises an exception if it gets all the way up to
   * an invalid kOperationIdInvalid value.
   */
  static OperationId GetNextOperationId() {
    static OperationId next_operation_id{};
    auto               this_id = next_operation_id;
    next_operation_id++;
    if (this_id == kOperationIdInvalid) {
      ASTL_LOG_CRITICAL("ASTL has run out of unique OperationIds. This error is currently unrecoverable");
      throw std::runtime_error("ASTL has run out of unique OperationIds. This error is currently unrecoverable");
    }
    return this_id;
  }
};

using OperationSequence = std::vector<std::unique_ptr<Operation>>;

}  // namespace astl

#endif  // OPERATION_HPP_