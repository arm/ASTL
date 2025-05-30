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
#include "counter.hpp"
#include "operation.hpp"
#include "target.hpp"

namespace astl {

struct SampledData {
  SampledData() = delete;

  SampledData(ICounter *counter, astl_value_t value)
      : counter{counter},
        value{value},
        timestamp{std::chrono::time_point_cast<SampleTimestamp::duration>(std::chrono::steady_clock::now())} {}

  SampledData(ICounter *counter, astl_value_t value, std::chrono::time_point<std::chrono::steady_clock> timestamp)
      : counter{counter}, value{value}, timestamp{std::chrono::time_point_cast<SampleTimestamp::duration>(timestamp)} {}

  ICounter       *counter = nullptr;  // - needs to be serializable, use an ID instead?
  astl_value_t    value;
  SampleTimestamp timestamp;
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
