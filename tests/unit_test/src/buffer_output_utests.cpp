// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "config/astl_configuration.hpp"
#include "output/buffer_output.hpp"
#include "target.hpp"

using trompeloeil::_;

// NOLINTBEGIN(readability-function-cognitive-complexity)  (catch2 tests with SECTIONs look complex to a linter)

// Avoid polluting global namespace: explicitly qualify astl symbols below.
namespace {
// Helper to build a ProcessedSampledData with a concrete numeric value and deterministic timestamp
astl::ProcessedSampledData MakeProcessedSample(uint64_t value, astl::ProcessedSampleTimestamp timestamp) {
  return astl::ProcessedSampledData{astl::AstlValue{value}, timestamp};
}
}  // namespace

TEST_CASE("BufferOutput writes exact capacity with success", "[buffer_output]") {
  // Arrange
  constexpr uint32_t                  capacity = 3;
  std::array<astl_sample_t, capacity> backing{};
  uint32_t                            sample_count_capacity = capacity;  // preset capacity per contract
  astl::BufferOutput                  output(std::span<astl_sample_t>(backing), &sample_count_capacity);

  // Build three processed samples
  const astl::ProcessedSampleTimestamp    base_ts{};  // epoch
  std::vector<astl::ProcessedSampledData> samples;
  samples.emplace_back(MakeProcessedSample(11U, base_ts));
  samples.emplace_back(MakeProcessedSample(22U, base_ts + astl::ProcessedSampleTimestamp::duration{1}));
  samples.emplace_back(MakeProcessedSample(33U, base_ts + astl::ProcessedSampleTimestamp::duration{2}));

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(samples));

  // Assert
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(sample_count_capacity == capacity);

  REQUIRE(backing.size() == samples.size());

  auto require_processed_sample_matches_metric_sample = [](const astl_sample_t&              metric_sample,
                                                           const astl::ProcessedSampledData& processed_sample) {
    REQUIRE(metric_sample.timestamp == static_cast<uint64_t>(processed_sample.timestamp.time_since_epoch().count()));
    auto [sample_value, sample_value_type] = processed_sample.value.ToAstlUnionValue();
    (void)sample_value_type;
    REQUIRE(metric_sample.value.ui64 == sample_value.ui64);
    return metric_sample;
  };

  std::transform(backing.cbegin(), backing.cend(), samples.cbegin(), backing.begin(),
                 require_processed_sample_matches_metric_sample);
}

TEST_CASE("BufferOutput returns BUFFER_LARGER_THAN_NEEDED when slack remains", "[buffer_output]") {
  // Arrange
  constexpr uint32_t                  capacity = 5;  // buffer bigger than sample set
  std::array<astl_sample_t, capacity> backing{};
  uint32_t                            sample_count_capacity = capacity;  // provide capacity
  astl::BufferOutput                  output(std::span<astl_sample_t>(backing), &sample_count_capacity);

  std::vector<astl::ProcessedSampledData> samples;
  const astl::ProcessedSampleTimestamp    base_ts{};
  samples.emplace_back(MakeProcessedSample(1U, base_ts));
  samples.emplace_back(MakeProcessedSample(2U, base_ts + astl::ProcessedSampleTimestamp::duration{10}));

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(samples));

  // Assert
  REQUIRE(status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  REQUIRE(sample_count_capacity == samples.size());
  REQUIRE(backing[0].value.ui64 == 1U);
  REQUIRE(backing[1].value.ui64 == 2U);
}

TEST_CASE("BufferOutput fails when provided capacity smaller than samples", "[buffer_output]") {
  // Arrange
  constexpr uint32_t                  capacity = 2;  // claim capacity smaller than actual sample list
  std::array<astl_sample_t, capacity> backing{};
  uint32_t                            sample_count_capacity = capacity;  // preset capacity
  astl::BufferOutput                  output(std::span<astl_sample_t>(backing), &sample_count_capacity);

  std::vector<astl::ProcessedSampledData> samples;
  const astl::ProcessedSampleTimestamp    base_ts{};
  samples.emplace_back(MakeProcessedSample(10U, base_ts));
  samples.emplace_back(MakeProcessedSample(20U, base_ts + astl::ProcessedSampleTimestamp::duration{1}));
  samples.emplace_back(MakeProcessedSample(30U, base_ts + astl::ProcessedSampleTimestamp::duration{2}));

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
  std::array<astl_sample_t, 2>            backing{};
  astl::BufferOutput                      output(std::span<astl_sample_t>(backing), nullptr);
  std::vector<astl::ProcessedSampledData> samples;
  const astl::ProcessedSampleTimestamp    sample_ts{};
  samples.emplace_back(MakeProcessedSample(5U, sample_ts));

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(samples));

  // Assert
  REQUIRE(status == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("BufferOutput writing zero samples returns BUFFER_LARGER_THAN_NEEDED (slack)", "[buffer_output]") {
  // Arrange
  std::array<astl_sample_t, 4>            backing{};
  uint32_t                                capacity = static_cast<uint32_t>(backing.size());
  astl::BufferOutput                      output(std::span<astl_sample_t>(backing), &capacity);
  std::vector<astl::ProcessedSampledData> empty;

  // Act
  auto status = output.WriteProcessedSamples(std::span<const astl::ProcessedSampledData>(empty));

  // Assert
  // With zero writes and positive backing size, currently treated as slack.
  REQUIRE(status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
  REQUIRE(capacity == 0);  // implementation resets count to 0 before loop
}

// NOLINTEND(readability-function-cognitive-complexity)  (catch2 tests with SECTIONs look complex to a linter)
