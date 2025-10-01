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
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "target.hpp"

namespace astl {

using SamplingInterval = std::chrono::duration<uint32_t, std::milli>;
// unsigned representation for timestamps - not suitable for differences
using SampleMicroseconds = std::chrono::duration<uint64_t, std::micro>;
using SampleTimestamp    = std::chrono::time_point<std::chrono::steady_clock, SampleMicroseconds>;

using OperationId                    = uint16_t;
constexpr size_t kOperationIdInvalid = std::numeric_limits<OperationId>::max();

/**
 * @brief Base class for concrete operations executed by collectors to enable or sample metrics.
 *
 * Each operation instance receives a monotonically increasing 16-bit identifier used to
 * correlate raw samples with higher-level metric processing. If the ID space is exhausted
 * (wraps to the sentinel invalid value) an exception is thrown because continued correlation
 * would no longer be reliable.
 */
class Operation {
 public:
  virtual ~Operation() = default;

  Operation() : operation_id{GetNextOperationId()} {}
  Operation(const Operation&)            = default;
  Operation& operator=(const Operation&) = default;
  Operation(Operation&&)                 = default;
  Operation& operator=(Operation&&)      = default;

  /**
   * @brief Get the unique identifier for this operation instance.
   * Useful for when a Collector needs to send SampledData back to Orchestrator and tie it to Metrics
   *
   * @return OperationId value (never equal to kOperationIdInvalid during a valid lifetime).
   */
  OperationId GetId() const { return operation_id; }

 private:
  OperationId operation_id{kOperationIdInvalid};

  /**
   * @brief Atomically fetch-and-increment the global operation ID counter.
   *
   * Since this is used in the constructor, this simply raises an exception if it gets all the way up to
   * an invalid kOperationIdInvalid value.
   *
   * @throws std::runtime_error if the counter reaches kOperationIdInvalid.
   */
  static OperationId GetNextOperationId() {
    static std::atomic<OperationId> next_operation_id{0};
    auto                            this_id = next_operation_id.fetch_add(1, std::memory_order_relaxed);
    if (this_id == kOperationIdInvalid) {
      ASTL_LOG_CRITICAL("ASTL has run out of unique OperationIds. This error is currently unrecoverable");
      throw std::runtime_error("ASTL has run out of unique OperationIds. This error is currently unrecoverable");
    }
    return this_id;
  }
};

using OperationSequence = std::vector<std::unique_ptr<Operation>>;

/**
 * @brief OperationBuilder concept - interface for building operations for a given target
 */
template <typename OperationBuilderType>
concept OperationBuilder = requires(const OperationBuilderType& operation_builder, const ITarget* target) {
  { operation_builder.BuildOperations(target) } -> std::same_as<std::expected<OperationSequence, astl_status_code>>;
};

}  // namespace astl

#endif  // OPERATION_HPP_