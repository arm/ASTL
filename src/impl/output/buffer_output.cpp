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

#include "output/buffer_output.hpp"

#include <format>
#include <span>

#include "astl_logger.hpp"

namespace astl {

auto BufferOutput::WriteProcessedSamples(std::span<const ProcessedSampledData> samples) -> astl_status_code {
  if (_buffer_sample_count == nullptr) {
    ASTL_LOG_ERROR("BufferOutput: Buffer sample count pointer is null");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  // _buffer_sample_count is the capacity provided by caller; ensure span also matches at least that much
  const size_t capacity = static_cast<size_t>(*_buffer_sample_count);
  if (capacity < samples.size() || _samples_buffer.size() < samples.size()) {
    ASTL_LOG_ERROR(
        "BufferOutput: Not enough space in buffer to write samples. Buffer capacity: {}, Buffer span size: {}, Samples "
        "size: {}",
        capacity, _samples_buffer.size(), samples.size());
    return ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL;
  }

  *_buffer_sample_count = 0;  // reset count before writing
  for (const auto& sample : samples) {
    const auto union_value                 = sample.value.ToAstlUnionValue().first;  // avoid constructing pair twice
    _samples_buffer[*_buffer_sample_count] = {._size      = sizeof(astl_metric_sample_t),
                                              ._timestamp = sample.timestamp.time_since_epoch().count(),
                                              ._value     = union_value};
    ++(*_buffer_sample_count);
  }

  // We treat extra unused capacity as non-fatal and signal with BUFFER_LARGER_THAN_NEEDED
  return (_samples_buffer.size() > *_buffer_sample_count) ? ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED : ASTL_STATUS_SUCCESS;
}

}  // namespace astl
