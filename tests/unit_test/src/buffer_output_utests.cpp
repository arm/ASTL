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

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "config/astl_configuration.hpp"
#include "output/buffer_output.hpp"
#include "target.hpp"

using trompeloeil::_;

// Avoid polluting global namespace: explicitly qualify astl symbols below.

namespace {
// Helper to build a ProcessedSampledData with a concrete numeric value and deterministic timestamp
astl::ProcessedSampledData MakeProcessedSample(uint64_t value, astl::SampleTimestamp timestamp) {
  return astl::ProcessedSampledData{astl::AstlValue{value}, timestamp};
}
}  // namespace

TEST_CASE("BufferOutput writes exact capacity with success", "[buffer_output]") {
  // Arrange
  constexpr uint32_t                         capacity = 3;
  std::array<astl_metric_sample_t, capacity> backing{};
  uint32_t                                   sample_count_capacity = capacity;  // preset capacity per contract
  astl::BufferOutput                         output(std::span<astl_metric_sample_t>(backing), &sample_count_capacity);

  // Build three processed samples
  const astl::SampleTimestamp             base_ts{};  // epoch
  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(MakeProcessedSample(11U, base_ts));
  samples.emplace_back(MakeProcessedSample(22U, base_ts + astl::SampleTimestamp::duration{1}));
  samples.emplace_back(MakeProcessedSample(33U, base_ts + astl::SampleTimestamp::duration{2}));

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(samples));

  // Assert
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(sample_count_capacity == capacity);
  for (size_t i = 0; i < samples.size(); ++i) {  // NOLINT(modernize-loop-convert)
    REQUIRE(backing[i]._size ==
            sizeof(astl_metric_sample_t));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    // Extract the union value written by BufferOutput and compare with original sample's variant
    auto original_pair = samples[i].value.ToAstlUnionValue();
    REQUIRE(backing[i]._value.ui64 == original_pair.first.ui64);  // NOLINT(cppcoreguidelines-pro-type-union-access,
                                                                  // cppcoreguidelines-pro-bounds-constant-array-index)
    REQUIRE(
        backing[i]._timestamp ==
        samples[i].timestamp.time_since_epoch().count());  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
  }
}

TEST_CASE("BufferOutput returns BUFFER_LARGER_THAN_NEEDED when slack remains", "[buffer_output]") {
  // Arrange
  constexpr uint32_t                         capacity = 5;  // buffer bigger than sample set
  std::array<astl_metric_sample_t, capacity> backing{};
  uint32_t                                   sample_count_capacity = capacity;  // provide capacity
  astl::BufferOutput                         output(std::span<astl_metric_sample_t>(backing), &sample_count_capacity);

  std::vector<astl::ProcessedSampledData> samples;
  const astl::SampleTimestamp             base_ts{};
  samples.emplace_back(MakeProcessedSample(1U, base_ts));
  samples.emplace_back(MakeProcessedSample(2U, base_ts + astl::SampleTimestamp::duration{10}));

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(samples));

  // Assert
  REQUIRE(status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  REQUIRE(sample_count_capacity == samples.size());
  REQUIRE(backing[0]._value.ui64 == 1U);
  REQUIRE(backing[1]._value.ui64 == 2U);
}

TEST_CASE("BufferOutput fails when provided capacity smaller than samples", "[buffer_output]") {
  // Arrange
  constexpr uint32_t                         capacity = 2;  // claim capacity smaller than actual sample list
  std::array<astl_metric_sample_t, capacity> backing{};
  uint32_t                                   sample_count_capacity = capacity;  // preset capacity
  astl::BufferOutput                         output(std::span<astl_metric_sample_t>(backing), &sample_count_capacity);

  std::vector<astl::ProcessedSampledData> samples;
  const astl::SampleTimestamp             base_ts{};
  samples.emplace_back(MakeProcessedSample(10U, base_ts));
  samples.emplace_back(MakeProcessedSample(20U, base_ts + astl::SampleTimestamp::duration{1}));
  samples.emplace_back(MakeProcessedSample(30U, base_ts + astl::SampleTimestamp::duration{2}));

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(samples));

  // Assert
  REQUIRE(status == ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL);
  // Contract: implementation performs no partial writes on insufficient capacity.
  // sample_count_capacity may remain as original capacity or reset to 0 (implementation resets only on success path),
  // so enforce not equal to samples.size() to detect accidental partial success.
  REQUIRE(sample_count_capacity != samples.size());
}

TEST_CASE("BufferOutput handles null count pointer (internal error)", "[buffer_output]") {
  // Arrange
  std::array<astl_metric_sample_t, 2>     backing{};
  astl::BufferOutput                      output(std::span<astl_metric_sample_t>(backing), nullptr);
  std::vector<astl::ProcessedSampledData> samples;
  const astl::SampleTimestamp             ts{};
  samples.emplace_back(MakeProcessedSample(5U, ts));

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(samples));

  // Assert
  REQUIRE(status == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("BufferOutput writing zero samples returns BUFFER_LARGER_THAN_NEEDED (slack)", "[buffer_output]") {
  // Arrange
  std::array<astl_metric_sample_t, 4>     backing{};
  uint32_t                                capacity = backing.size();
  astl::BufferOutput                      output(std::span<astl_metric_sample_t>(backing), &capacity);
  std::vector<astl::ProcessedSampledData> empty;

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(empty));

  // Assert
  // With zero writes and positive backing size, currently treated as slack.
  REQUIRE(status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  REQUIRE(capacity == 0);  // implementation resets count to 0 before loop
}