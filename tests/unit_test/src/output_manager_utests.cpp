// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <chrono>
#include <cstdlib>
#include <vector>

#include "../../test_includes.hpp"   // must come first before any Catch2 usage
#include "../../test_utilities.hpp"  // TempFileGuard
#include "common/i_processed_sample_sink.hpp"
#include "config/astl_configuration.hpp"
#include "metric/i_metric.hpp"
#include "operation/operation.hpp"
#include "output/buffer_output.hpp"
#include "output/output_manager.hpp"
#include "target.hpp"

namespace {
// Helper to make simple processed samples
std::vector<astl::ProcessedSampledData> MakeSamples(size_t n) {
  std::vector<astl::ProcessedSampledData> samples_vec;
  samples_vec.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    samples_vec.emplace_back(astl::AstlValue{static_cast<uint64_t>(i + 1)},
                             astl::SampleTimestamp{std::chrono::microseconds{100 + static_cast<int>(i)}});
  }
  return samples_vec;
}
}  // namespace

TEST_CASE("BufferOutput::WriteProcessedSamples basic behaviors", "[output_manager][buffer_output]") {  // NOLINT
  constexpr size_t                           capacity = 5;
  std::array<astl_metric_sample_t, capacity> buffer{};
  uint32_t           count_capacity = static_cast<uint32_t>(capacity);  // caller provided capacity
  astl::BufferOutput buffer_output(std::span<astl_metric_sample_t>(buffer), &count_capacity);

  SECTION("Null count pointer causes INTERNAL_ERROR") {
    astl::BufferOutput bad_output(std::span<astl_metric_sample_t>(buffer), nullptr);
    auto               samples = MakeSamples(2);
    REQUIRE(bad_output.WriteProcessedSamples(samples) == ASTL_STATUS_INTERNAL_ERROR);
  }

  SECTION("Span larger than samples returns BUFFER_LARGER_THAN_NEEDED") {
    auto samples   = MakeSamples(3);
    count_capacity = static_cast<uint32_t>(samples.size());  // capacity equals size
    REQUIRE(buffer_output.WriteProcessedSamples(samples) == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED);
    // Because underlying span size (5) > samples written (3), status is BUFFER_LARGER_THAN_NEEDED
    REQUIRE(count_capacity == samples.size());
    // verify count updated
    REQUIRE(count_capacity == samples.size());
  }

  SECTION("Buffer exactly filled returns SUCCESS (no extra capacity)") {
    std::array<astl_metric_sample_t, 3> tight_buffer{};
    uint32_t                            tight_capacity = 3;
    astl::BufferOutput                  tight_output(std::span<astl_metric_sample_t>(tight_buffer), &tight_capacity);
    auto                                samples = MakeSamples(3);
    tight_capacity                              = static_cast<uint32_t>(samples.size());
    REQUIRE(tight_output.WriteProcessedSamples(samples) == ASTL_STATUS_SUCCESS);
    REQUIRE(tight_capacity == samples.size());
  }

  SECTION("Capacity smaller than samples returns METRIC_SAMPLES_BUFFER_TOO_SMALL") {
    auto samples   = MakeSamples(4);
    count_capacity = 3;  // claim only space for 3
    REQUIRE(buffer_output.WriteProcessedSamples(samples) == ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL);
  }
}

TEST_CASE("OutputManager::Create/DestroyBufferOutput lifecycle", "[output_manager]") {  // NOLINT
  astl::OutputManager                 mgr;
  std::array<astl_metric_sample_t, 4> buf{};
  uint32_t                            buf_capacity = 4;

  SECTION("CreateBufferOutput succeeds with non-empty span") {
    REQUIRE(mgr.CreateBufferOutput(std::span<astl_metric_sample_t>(buf), &buf_capacity) == ASTL_STATUS_SUCCESS);
  }

  SECTION("CreateBufferOutput with empty span fails") {
    std::span<astl_metric_sample_t> empty_span{};
    REQUIRE(mgr.CreateBufferOutput(empty_span, &buf_capacity) == ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("DestroyBufferOutput is idempotent") {
    REQUIRE(mgr.DestroyBufferOutput() == ASTL_STATUS_SUCCESS);  // no output yet
    REQUIRE(mgr.CreateBufferOutput(std::span<astl_metric_sample_t>(buf), &buf_capacity) == ASTL_STATUS_SUCCESS);
    REQUIRE(mgr.DestroyBufferOutput() == ASTL_STATUS_SUCCESS);
    REQUIRE(mgr.DestroyBufferOutput() == ASTL_STATUS_SUCCESS);
  }
}

// Minimal mock target + metric to populate ProcessedSamplesMap without depending on full mocks (lighter weight)
struct TinyTarget : public astl::ITarget {
  std::string         name{"T0"};
  astl::CollectorType collector_type{astl::CollectorType::SCMI};
  auto                GetCollectorType() const -> astl::CollectorType override { return collector_type; }
  auto                Name() const -> std::string const& override { return name; }
  auto                GetProperties(astl_target_properties_t* props) const -> astl_status_code override {
    if (!props) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    props->_handle = this;
    return ASTL_STATUS_SUCCESS;
  }
};

struct TinyMetric : public astl::IMetric {
  std::string name{"M0"};
  bool        CheckCapabilities(const astl::Capabilities& caps) const override {
    (void)caps;
    return true;
  }
  std::expected<astl::OperationSequence, astl_status_code> GetOperations() override {
    return astl::OperationSequence{};
  }
  astl_status_code ReceiveRawSample(const astl::RawSampledData& sample) override {
    (void)sample;
    return ASTL_STATUS_SUCCESS;
  }
  void             SetProcessedSampleSink(astl::IProcessedSampleSink* sink) override { (void)sink; }
  void             Reset() override {}
  astl_status_code Summarize() override { return ASTL_STATUS_SUCCESS; }
  astl_status_code GetProperties(astl_metric_properties_t* props) const override {
    (void)props;
    return ASTL_STATUS_SUCCESS;
  }
  auto             Name() const -> std::string const& override { return name; }
  astl_status_code SinkProcessedSample(const astl::ProcessedSampledData& processed_sample) override {
    (void)processed_sample;
    return ASTL_STATUS_SUCCESS;
  }
};

TEST_CASE("OutputManager::OutputProcessedSamples error paths", "[output_manager]") {  // NOLINT
  astl::OutputManager                 mgr;
  std::array<astl_metric_sample_t, 8> out_buf{};
  uint32_t                            out_capacity = 8;
  // Create buffer output for writing
  REQUIRE(mgr.CreateBufferOutput(std::span<astl_metric_sample_t>(out_buf), &out_capacity) == ASTL_STATUS_SUCCESS);

  TinyTarget target;
  TinyMetric metric;

  astl::ProcessedSamplesMap processed_samples;  // empty initially

  SECTION("Missing target in map") {
    REQUIRE(mgr.OutputProcessedSamples(processed_samples, astl::OutputType::BUFFER, &target, &metric) ==
            ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("Target present but metric missing") {
    processed_samples[&target];  // insert target with empty metric map
    REQUIRE(mgr.OutputProcessedSamples(processed_samples, astl::OutputType::BUFFER, &target, &metric) ==
            ASTL_STATUS_BAD_ARGUMENT);
  }

  SECTION("Metric present but samples empty") {
    processed_samples[&target][&metric] = {};  // empty samples vector
    REQUIRE(mgr.OutputProcessedSamples(processed_samples, astl::OutputType::BUFFER, &target, &metric) ==
            ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE("OutputManager::OutputProcessedSamples success path", "[output_manager]") {  // NOLINT
  astl::OutputManager                 mgr;
  std::array<astl_metric_sample_t, 8> out_buf{};
  uint32_t                            out_capacity = 8;
  REQUIRE(mgr.CreateBufferOutput(std::span<astl_metric_sample_t>(out_buf), &out_capacity) == ASTL_STATUS_SUCCESS);

  TinyTarget                target;
  TinyMetric                metric;
  auto                      samples = MakeSamples(3);
  astl::ProcessedSamplesMap processed_samples;
  processed_samples[&target][&metric] = samples;

  auto status = mgr.OutputProcessedSamples(processed_samples, astl::OutputType::BUFFER, &target, &metric);
  REQUIRE((status == ASTL_STATUS_SUCCESS || status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED));
  REQUIRE(out_capacity == samples.size());
  REQUIRE(out_capacity == samples.size());
}

TEST_CASE("OutputManager::EnsurePerfettoOutput and EnsureIntervalCsvOutput env var error",
          "[output_manager]") {  // NOLINT
  astl::OutputManager mgr;
  // Unset env vars (best effort) using ASTL helper (empty value treated as unset in code paths)
  (void)astl::SetEnvVar(astl::EnvVar::ASTL_OUTPUT_PERFETTO, "");
  (void)astl::SetEnvVar(astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, "");
  // Direct ensure calls should fail with BAD_ARGUMENT when vars missing
  REQUIRE(mgr.OutputProcessedSamples(astl::ProcessedSamplesMap{}, astl::OutputType::PERFETTO, nullptr, nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
  REQUIRE(mgr.OutputProcessedSamples(astl::ProcessedSamplesMap{}, astl::OutputType::INTERVAL_CSV, nullptr, nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
}

TEST_CASE("OutputManager::EnsureIntervalCsvOutput success", "[output_manager][intervalcsv]") {  // NOLINT
  astl::OutputManager mgr;
  TempFileGuard       tmp_guard{"om_intervalcsv_success.csv"};
  REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, tmp_guard.path.string()) == ASTL_STATUS_SUCCESS);
  // Empty processed map still results in writer creation success
  astl::ProcessedSamplesMap empty;
  REQUIRE(mgr.OutputProcessedSamples(empty, astl::OutputType::INTERVAL_CSV, nullptr, nullptr) == ASTL_STATUS_SUCCESS);
  // File should exist (may be empty)
  std::ifstream ifs(tmp_guard.path);
  REQUIRE(ifs.is_open());
}

TEST_CASE("OutputManager::OutputProcessedSamples PERFETTO success", "[output_manager][perfetto]") {  // NOLINT
  astl::OutputManager mgr;
  TempFileGuard       tmp_guard{"om_perfetto_success.json"};
  REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_OUTPUT_PERFETTO, tmp_guard.path.string()) == ASTL_STATUS_SUCCESS);
  // Build minimal processed samples map with one sample
  TinyTarget                target;
  TinyMetric                metric;
  astl::ProcessedSamplesMap processed;
  processed[&target][&metric].push_back(astl::ProcessedSampledData{
      astl::AstlValue{static_cast<uint64_t>(42)}, astl::SampleTimestamp{std::chrono::microseconds{12345}}});
  REQUIRE(mgr.OutputProcessedSamples(processed, astl::OutputType::PERFETTO, nullptr, nullptr) == ASTL_STATUS_SUCCESS);
  std::ifstream ifs(tmp_guard.path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(content.empty());
}

TEST_CASE("OutputManager::OutputProcessedSamples INTERVAL_CSV success with sample",
          "[output_manager][intervalcsv]") {  // NOLINT
  astl::OutputManager mgr;
  TempFileGuard       tmp_guard{"om_intervalcsv_sample.csv"};
  REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_OUTPUT_INTERVAL_CSV, tmp_guard.path.string()) == ASTL_STATUS_SUCCESS);
  TinyTarget                target;
  TinyMetric                metric;
  astl::ProcessedSamplesMap processed;
  processed[&target][&metric].push_back(astl::ProcessedSampledData{
      astl::AstlValue{static_cast<uint64_t>(7)}, astl::SampleTimestamp{std::chrono::microseconds{98765}}});
  REQUIRE(mgr.OutputProcessedSamples(processed, astl::OutputType::INTERVAL_CSV, nullptr, nullptr) ==
          ASTL_STATUS_SUCCESS);
  std::ifstream ifs(tmp_guard.path);
  REQUIRE(ifs.is_open());
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("timestamp_us,target,metric,value") != std::string::npos);
}

TEST_CASE("OutputManager::OutputProcessedSamples unknown output type", "[output_manager]") {  // NOLINT
  astl::OutputManager mgr;
  auto                unknown = static_cast<astl::OutputType>(999);
  REQUIRE(mgr.OutputProcessedSamples(astl::ProcessedSamplesMap{}, unknown, nullptr, nullptr) ==
          ASTL_STATUS_BAD_ARGUMENT);
}
