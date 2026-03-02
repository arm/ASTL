// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <vector>

#include "../../test_includes.hpp"  // project test harness aggregates Catch2
#include "common/astl_value.hpp"
#include "common/i_processed_sample_sink.hpp"

// Avoid global namespace pollution; qualify astl symbols explicitly.

TEST_CASE("ProcessedSampledData default timestamp constructor assigns near-now timestamp", "[processed_sample]") {
  auto before = std::chrono::time_point_cast<astl::SampleTimestamp::duration>(std::chrono::steady_clock::now());
  astl::ProcessedSampledData sample{astl::AstlValue{uint64_t{42}}};
  auto after = std::chrono::time_point_cast<astl::SampleTimestamp::duration>(std::chrono::steady_clock::now());
  REQUIRE(sample.value.IsArithmetic());
  REQUIRE(sample.get<uint64_t>() == 42);
  REQUIRE_FALSE(sample.timestamp < before);
  REQUIRE_FALSE(sample.timestamp > after);
}

TEST_CASE("ProcessedSampledData explicit timestamp constructor preserves timestamp", "[processed_sample]") {
  astl::SampleTimestamp      timestamp{astl::SampleTimestamp::duration{123456}};
  astl::ProcessedSampledData sample{astl::AstlValue{uint32_t{7}}, timestamp};
  REQUIRE(sample.get<uint32_t>() == 7);
  REQUIRE(sample.timestamp == timestamp);
}

TEST_CASE("ProcessedSampledData get<T>() throws on wrong type", "[processed_sample]") {
  astl::ProcessedSampledData sample{astl::AstlValue{uint16_t{9}}};
  REQUIRE_NOTHROW(sample.get<uint16_t>());
  REQUIRE_THROWS_AS(sample.get<uint32_t>(), std::bad_variant_access);
}

struct RecordingSink : astl::IProcessedSampleSink {
  const astl::ITarget*                    lastTarget{nullptr};
  const astl::IMetric*                    lastMetric{nullptr};
  std::vector<astl::ProcessedSampledData> received{};  // explicit default initialization for clarity
  astl_status_code                        SinkProcessedSamples(const astl::ITarget* target, const astl::IMetric* metric,
                                                               std::span<const astl::ProcessedSampledData> samples) override {
    lastTarget = target;
    lastMetric = metric;
    received.insert(received.end(), samples.begin(), samples.end());
    return ASTL_STATUS_SUCCESS;
  }
};

TEST_CASE("IProcessedSampleSink receives multiple samples (nullptr target/metric allowed for test)",
          "[processed_sample][sink]") {
  RecordingSink                           sink;
  std::vector<astl::ProcessedSampledData> samples;
  astl::SampleTimestamp                   base{astl::SampleTimestamp::duration{1000}};
  samples.emplace_back(astl::AstlValue{uint8_t{1}}, base);
  samples.emplace_back(astl::AstlValue{uint8_t{2}},
                       astl::SampleTimestamp{base.time_since_epoch() + astl::SampleTimestamp::duration{1}});
  auto status = sink.SinkProcessedSamples(nullptr, nullptr, samples);
  REQUIRE(status == ASTL_STATUS_SUCCESS);
  REQUIRE(sink.received.size() == 2);
  REQUIRE(sink.received[0].get<uint8_t>() == 1);
  REQUIRE(sink.received[1].get<uint8_t>() == 2);
  REQUIRE(sink.lastTarget == nullptr);
  REQUIRE(sink.lastMetric == nullptr);
  REQUIRE(sink.received[0].timestamp < sink.received[1].timestamp);
}
