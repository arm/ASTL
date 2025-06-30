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

#ifndef I_SAMPLE_SINK_HPP_
#define I_SAMPLE_SINK_HPP_

#include <chrono>
#include <span>
#include <utility>

#include "astl/astl.h"
#include "astl_value.hpp"
#include "counter.hpp"
#include "operation.hpp"
#include "target.hpp"

namespace astl {

struct SampledData {
  SampledData() = delete;

  SampledData(OperationId operation_id, AstlValue value)
      : operation_id{operation_id},
        value{value},
        timestamp{std::chrono::time_point_cast<SampleTimestamp::duration>(std::chrono::steady_clock::now())} {}

  SampledData(OperationId operation_id, AstlValue value, SampleTimestamp timestamp)
      : operation_id{operation_id}, value{value}, timestamp{timestamp} {}

  OperationId     operation_id{kOperationIdInvalid};
  AstlValue       value;
  SampleTimestamp timestamp;

  // get the raw data of type T (must be exact match)
  template <typename T>
  const auto &get() const {
    return std::get<T>(value.value);
  };
};

/* ISampleSink is an interface for anything that can receive sampled data.
 * This might include the Orchestrator, CollectorManager, or output writers, as well as test components.
 */
struct ISampleSink {
  virtual ~ISampleSink() = default;

  ISampleSink()                               = default;
  ISampleSink(const ISampleSink &)            = default;
  ISampleSink &operator=(ISampleSink const &) = default;
  ISampleSink(ISampleSink &&)                 = default;
  ISampleSink &operator=(ISampleSink &&)      = default;

  /*
   * Deliver Some number of samples collected from the given target to this ISampleSink
   */
  virtual astl_status_code SinkSamples(ITarget *target, std::span<SampledData> samples) = 0;
};

}  // namespace astl

#endif  // I_SAMPLE_SINK_HPP_
