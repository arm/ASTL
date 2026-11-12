// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "metric/metric_manager.hpp"
#include "scmi/scmi_operation_builder.hpp"

namespace {
#if defined(__linux__) && !defined(ASTL_MEMORY_TEST_INSTRUMENTED)
// Tune the workload and permitted growth here, without changing the test's command line.
constexpr double      kMaxMemoryFactor    = 1.2;
constexpr std::size_t kMemorySampleCount  = 10;
constexpr uint64_t    kMeasuredSamples    = 8'000'000;
constexpr uint64_t    kWarmupSamples      = 100'000;
constexpr uint64_t    kRetrievalBatchSize = 256;
static_assert(kMaxMemoryFactor > 1.0);
static_assert(kMemorySampleCount > 0 && kMeasuredSamples % kMemorySampleCount == 0);

// smaps_rollup reports current resident memory across all mappings, rather than a historical peak.
auto ReadResidentKiB() -> std::optional<uint64_t> {
  std::ifstream input{"/proc/self/smaps_rollup"};
  std::string   line;
  while (std::getline(input, line)) {
    if (line.starts_with("Rss:")) {
      std::istringstream fields{line};
      std::string        name;
      std::string        units;
      uint64_t           rss{0};
      if (fields >> name >> rss >> units && units == "kB" && rss > 0) {
        return rss;
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}
#endif

// Each CTest registration runs this workload in its own process. No hardware, sleeps, or retained caller results.
auto CheckCollectionMemory(bool residency) -> void {
#if !defined(__linux__) || defined(ASTL_MEMORY_TEST_INSTRUMENTED)
  (void)residency;
  SKIP("Process RSS regression requires Linux and an uninstrumented allocator");
#else
  if (!ReadResidentKiB()) {
    SKIP("Current process RSS is unavailable from /proc/self/smaps_rollup");
  }
  EnvVarGuard raw_logging{astl::EnvVar::ASTL_LOG_RAW_SAMPLES};
  (void)astl::UnsetEnvVar(astl::EnvVar::ASTL_LOG_RAW_SAMPLES);
  EnvVarGuard log_level{astl::EnvVar::ASTL_LOG_LEVEL, "off"};
  using trompeloeil::_;

  auto              target_uptr = std::make_unique<MockTarget>();
  auto*             target      = target_uptr.get();
  const std::string target_name{"memory-regression-target"};
  ALLOW_CALL(*target, GetCollectorType()).RETURN(astl::CollectorType::SCMI);
  ALLOW_CALL(*target, Name()).RETURN(target_name);
  ALLOW_CALL(*target, GetProperties(_)).SIDE_EFFECT(_1->handle = target).RETURN(ASTL_STATUS_SUCCESS);
  auto                                         topology = std::make_unique<MockTopologyManager>();
  std::vector<std::unique_ptr<astl::ITarget> > targets;
  targets.push_back(std::move(target_uptr));
  REQUIRE(topology->SetTargets(std::move(targets)) == ASTL_STATUS_SUCCESS);

  auto collector = std::make_unique<MockCollectorManager>();
  ALLOW_CALL(*collector, RegisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector, UnregisterRawSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector, GetNativeClockSnapshot(_)).RETURN(astl::ClockCorrelationMap{});
  ALLOW_CALL(*collector, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector, StopOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(*collector, IsAnyTargetBeingCollected()).RETURN(false);

  const astl::Capabilities            capabilities{{astl::CollectorCapability{astl::CollectorType::SCMI}}, {}};
  auto                                manager = std::make_unique<astl::MetricManager>(capabilities);
  std::unique_ptr<astl::MetricConfig> config;
  if (residency) {
    astl::ResidencyMetricConfig::StateToInfoMap states{
        {"Active", {"Active", "Synthetic active ticks", 1'000'000.0, astl::ScmiOperationBuilder{1}}}
    };
    config = std::make_unique<astl::ResidencyMetricConfig>(
        "memory-residency", "Synthetic residency", ASTL_UNITS_TICKS, ASTL_VALUE_UINT64, ASTL_METRIC_RESIDENCY,
        ASTL_METRIC_IDENTIFIER_UNKNOWN, astl::CollectorType::SCMI, states,
        astl::ResidencyMetricConfig::InferredStateInfo{"Idle", "Inferred idle time"});
  } else {
    config = std::make_unique<astl::MetricConfig>("memory-value", "Synthetic temperature", ASTL_UNITS_CELSIUS,
                                                  ASTL_VALUE_UINT64, ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE,
                                                  astl::CollectorType::SCMI, astl::ScmiOperationBuilder{1});
  }
  REQUIRE(manager->RegisterMetric(std::move(config), {target}) == ASTL_STATUS_SUCCESS);
  const auto handles = manager->GetAvailableMetrics(target);
  REQUIRE(handles.has_value());
  REQUIRE(handles->size() == 1);
  const auto* const handle = handles->front();
  auto              metric = manager->GetMetricOnTarget(handle, target);
  REQUIRE(metric.has_value());

  auto orchestrator = std::make_unique<astl::Orchestrator>(
      std::move(topology), std::move(collector), std::move(manager), std::make_unique<MockOutputManager>(), "");
  auto*                    orchestrator_ptr = orchestrator.get();
  TestOrchestratorInjector injector{std::move(orchestrator)};
  ASTL_INIT_STRUCT(astl_collection_params_t, collection, .flags = ASTL_NO_CACHING, .sampling_interval = 1,
                   .collection_mode = ASTL_COLLECTION_MODE_SAMPLING);
  const std::array<astl_metric_handle_t, 1> selected{handle};
  REQUIRE(orchestrator_ptr->ConfigureMetricCollection(target, &collection, selected) == ASTL_STATUS_SUCCESS);
  REQUIRE(orchestrator_ptr->StartCollection(target) == ASTL_STATUS_SUCCESS);

  // Configuration resets residency bindings. Rebuild them for direct normalized-input injection;
  // this fixture exercises metrics and retrieval, without collector routing or clock conversion.
  auto operations = (*metric)->GetOperations();
  REQUIRE(operations.has_value());
  REQUIRE(operations->size() == 1);
  const auto operation_id = operations->front()->GetId();

  uint64_t sequence{0};
  auto     feed = [&] {
    ++sequence;
    const auto timestamp =
        astl::ProcessedSampleTimestamp{std::chrono::microseconds{static_cast<int64_t>(2 * sequence)}};
    const auto value  = residency ? sequence : sequence % 100;
    const auto status = (*metric)->ReceiveRawSample({operation_id, astl::AstlValue{value}, timestamp});
    // Avoid millions of successful Catch assertions; errors still fail at their source.
    const auto expected = residency && sequence == 1 ? ASTL_STATUS_NO_NEW_SAMPLE : ASTL_STATUS_SUCCESS;
    if (status != expected) {
      FAIL("Synthetic sample processing failed: " << astlStatusString(status));
    }
  };
  // Residency needs one initial counter baseline before it emits one Active and one Idle sample per input.
  if (residency) {
    feed();
  }
  std::array<astl_sample_t, 2 * kRetrievalBatchSize> output{};
  auto                                               collect_and_retrieve = [&](uint64_t inputs) {
    while (inputs > 0) {
      const auto batch_size = std::min(inputs, kRetrievalBatchSize);
      for (uint64_t sample = 0; sample < batch_size; ++sample) {
        feed();
      }
      uint32_t count = static_cast<uint32_t>(output.size());
      REQUIRE(GetMetricSamplesOnTarget(target, handle, output.data(), &count) == ASTL_STATUS_SUCCESS);
      REQUIRE(count == batch_size * (residency ? 2 : 1));
      uint32_t remaining{0};
      REQUIRE(GetMetricSampleCountOnTarget(target, handle, &remaining) == ASTL_STATUS_SUCCESS);
      REQUIRE(remaining == 0);
      inputs -= batch_size;
    }
  };

  collect_and_retrieve(kWarmupSamples);
  REQUIRE((*metric)->Summarize() == ASTL_STATUS_SUCCESS);
  const auto baseline = ReadResidentKiB();
  REQUIRE(baseline.has_value());
  const auto                               limit = static_cast<double>(*baseline) * kMaxMemoryFactor;
  std::array<uint64_t, kMemorySampleCount> memory_samples{};
  bool                                     exceeded_limit{false};
  for (std::size_t checkpoint = 0; checkpoint < kMemorySampleCount; ++checkpoint) {
    collect_and_retrieve(kMeasuredSamples / kMemorySampleCount);
    const auto rss = ReadResidentKiB();
    REQUIRE(rss.has_value());
    memory_samples.at(checkpoint) = *rss;
    exceeded_limit                = exceeded_limit || static_cast<double>(*rss) > limit;
  }
  // Assert only after every checkpoint, before StopCollection or metric destruction can hide retention.
  std::ostringstream diagnostics;
  if (exceeded_limit) {
    diagnostics << "Post-warm-up RSS: " << *baseline << " KiB; limit: " << limit << " KiB (" << kMaxMemoryFactor
                << "x)\n"
                << std::setw(14) << "Input samples" << std::setw(14) << "RSS (KiB)" << '\n';
    for (std::size_t checkpoint = 0; checkpoint < kMemorySampleCount; ++checkpoint) {
      diagnostics << std::setw(14) << (checkpoint + 1) * (kMeasuredSamples / kMemorySampleCount) << std::setw(14)
                  << memory_samples.at(checkpoint) << '\n';
    }
  }
  INFO(diagnostics.str());
  REQUIRE_FALSE(exceeded_limit);
  REQUIRE(orchestrator_ptr->StopCollection(target) == ASTL_STATUS_SUCCESS);
#endif
}
}  // namespace

TEST_CASE("No-cache sampled-value collection has bounded process memory", "[memory][SampledValueMetric]") {
  CheckCollectionMemory(false);
}

TEST_CASE("No-cache residency collection has bounded process memory", "[memory][ResidencyMetric]") {
  CheckCollectionMemory(true);
}
