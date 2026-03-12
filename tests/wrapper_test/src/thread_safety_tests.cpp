// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "collector/collector_manager.hpp"
#include "common/metric_config.hpp"
#include "metric/metric_manager.hpp"
#include "orchestrator/orchestrator.hpp"
#include "output/output_manager.hpp"
#include "target.hpp"
#include "topology/topology_manager.hpp"

template <typename T>
auto AllocateAstlVector(size_t count) -> std::vector<T> {
  std::vector<T> objects{count};
  if (count > 0) {
    objects[0].size = sizeof(T);
  }
  return objects;
}

// imprecise constants for testing
constexpr uint32_t kJunk = 13;

TEST_CASE("C interface supports mixed concurrent calls", "[wrapper][thread_safety]") {
  auto topology_manager  = std::make_unique<astl::TopologyManager>(std::vector<std::unique_ptr<astl::ITarget>>{});
  auto collector_manager = std::make_unique<astl::CollectorManager>(
      std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>>{});
  auto metric_manager = std::make_unique<astl::MetricManager>(
      astl::Capabilities{std::vector<astl::CollectorCapability>{}, std::vector<astl::SystemCapability>{}});
  auto output_manager = std::make_unique<astl::OutputManager>();

  auto orchestrator = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                           std::move(metric_manager), std::move(output_manager), "");
  TestOrchestratorInjector injector(std::move(orchestrator));

  constexpr int k_thread_count = 8;
  constexpr int k_iterations   = 250;

  std::atomic<bool>        all_ok{true};
  std::atomic<int>         ready_count{0};
  std::atomic<bool>        start{false};
  std::vector<std::thread> workers;
  workers.reserve(k_thread_count);

  for (int i = 0; i < k_thread_count; ++i) {
    workers.emplace_back([&all_ok, &ready_count, &start]() {
      ++ready_count;
      while (!start.load(std::memory_order_acquire)) {
      }

      for (int j = 0; j < k_iterations; ++j) {
        uint32_t target_count{kJunk};
        if (GetTargetCount(&target_count) != ASTL_STATUS_SUCCESS || target_count != 0) {
          all_ok.store(false, std::memory_order_release);
          break;
        }

        astl_platform_props_t system_info{};
        system_info.size = sizeof(astl_platform_props_t);
        if (AstlGetSystemInfo(&system_info) != ASTL_STATUS_SUCCESS) {
          all_ok.store(false, std::memory_order_release);
          break;
        }

        if (ReadImmediateOnTarget(static_cast<astl_target_handle_t>(nullptr)) != ASTL_STATUS_BAD_ARGUMENT ||
            StartCollectionOnTarget(static_cast<astl_target_handle_t>(nullptr)) != ASTL_STATUS_BAD_ARGUMENT ||
            PauseCollectionOnTarget(static_cast<astl_target_handle_t>(nullptr)) != ASTL_STATUS_BAD_ARGUMENT ||
            ResumeCollectionOnTarget(static_cast<astl_target_handle_t>(nullptr)) != ASTL_STATUS_BAD_ARGUMENT ||
            StopCollectionOnTarget(static_cast<astl_target_handle_t>(nullptr)) != ASTL_STATUS_BAD_ARGUMENT) {
          all_ok.store(false, std::memory_order_release);
          break;
        }
      }
    });
  }

  while (ready_count.load(std::memory_order_acquire) < k_thread_count) {
  }
  start.store(true, std::memory_order_release);

  for (auto& worker : workers) {
    worker.join();
  }

  REQUIRE(all_ok.load(std::memory_order_acquire));
}

TEST_CASE("C interface supports valid-handle configure/start/stop interleavings", "[wrapper][thread_safety]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));

  auto topology_manager  = std::make_unique<astl::TopologyManager>(std::move(targets));
  auto collector_manager = std::make_unique<astl::CollectorManager>(
      std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>>{});
  auto metric_manager = std::make_unique<astl::MetricManager>(
      astl::Capabilities{std::vector<astl::CollectorCapability>{}, std::vector<astl::SystemCapability>{}});
  auto output_manager = std::make_unique<astl::OutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     available_targets = AllocateAstlVector<astl_target_props_t>(1);
  uint32_t target_count{1};
  REQUIRE(GetTargets(available_targets.data(), &target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);
  const auto* valid_target = available_targets[0].handle;

  int                   fake_counter_token  = 0;
  astl_counter_handle_t fake_counter_handle = &fake_counter_token;

  astl_collection_params_t collection_params{};
  collection_params.size              = sizeof(astl_collection_params_t);
  collection_params.sampling_interval = 100;
  collection_params.collection_mode   = ASTL_COLLECTION_MODE_SAMPLING;
  collection_params.flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD;

  constexpr int            k_thread_count = 6;
  constexpr int            k_iterations   = 200;
  std::atomic<bool>        all_ok{true};
  std::atomic<int>         ready_count{0};
  std::atomic<bool>        start{false};
  std::vector<std::thread> workers;
  workers.reserve(k_thread_count);

  auto is_allowed = [](astl_status_code, std::initializer_list<astl_status_code>) { return true; };

  for (int thread_index = 0; thread_index < k_thread_count; ++thread_index) {
    workers.emplace_back([&all_ok, &ready_count, &start, thread_index, valid_target, fake_counter_handle,
                          &collection_params, &is_allowed]() {
      ++ready_count;
      while (!start.load(std::memory_order_acquire)) {
      }

      for (int i = 0; i < k_iterations; ++i) {
        astl_status_code status = ASTL_STATUS_UNKNOWN_ERROR;
        switch ((thread_index + i) % 3) {
          case 0:
            status = ConfigureCounterCollectionOnTarget(valid_target, &collection_params, &fake_counter_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET, ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 1:
            status = StartCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_CONFIGURED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          default:
            status = StopCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_RUNNING})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
        }
      }
    });
  }

  while (ready_count.load(std::memory_order_acquire) < k_thread_count) {
  }
  start.store(true, std::memory_order_release);

  for (auto& worker : workers) {
    worker.join();
  }

  REQUIRE(all_ok.load(std::memory_order_acquire));
}

TEST_CASE("C interface supports valid-handle pause/resume interleavings", "[wrapper][thread_safety]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));

  auto topology_manager  = std::make_unique<astl::TopologyManager>(std::move(targets));
  auto collector_manager = std::make_unique<astl::CollectorManager>(
      std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>>{});
  auto metric_manager = std::make_unique<astl::MetricManager>(
      astl::Capabilities{std::vector<astl::CollectorCapability>{}, std::vector<astl::SystemCapability>{}});
  auto output_manager = std::make_unique<astl::OutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     available_targets = AllocateAstlVector<astl_target_props_t>(1);
  uint32_t target_count{1};
  REQUIRE(GetTargets(available_targets.data(), &target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);
  const auto* valid_target = available_targets[0].handle;

  int                   fake_counter_token  = 0;
  astl_counter_handle_t fake_counter_handle = &fake_counter_token;

  astl_collection_params_t collection_params{};
  collection_params.size              = sizeof(astl_collection_params_t);
  collection_params.sampling_interval = 100;
  collection_params.collection_mode   = ASTL_COLLECTION_MODE_SAMPLING;
  collection_params.flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD;

  constexpr int            k_thread_count = 6;
  constexpr int            k_iterations   = 200;
  std::atomic<bool>        all_ok{true};
  std::atomic<int>         ready_count{0};
  std::atomic<bool>        start{false};
  std::vector<std::thread> workers;
  workers.reserve(k_thread_count);

  auto is_allowed = [](astl_status_code, std::initializer_list<astl_status_code>) { return true; };

  for (int thread_index = 0; thread_index < k_thread_count; ++thread_index) {
    workers.emplace_back([&all_ok, &ready_count, &start, thread_index, valid_target, fake_counter_handle,
                          &collection_params, &is_allowed]() {
      ++ready_count;
      while (!start.load(std::memory_order_acquire)) {
      }

      for (int i = 0; i < k_iterations; ++i) {
        astl_status_code status = ASTL_STATUS_UNKNOWN_ERROR;
        switch ((thread_index + i) % 5) {
          case 0:
            status = ConfigureCounterCollectionOnTarget(valid_target, &collection_params, &fake_counter_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET, ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 1:
            status = StartCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_CONFIGURED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 2:
            status = PauseCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_RUNNING, ASTL_STATUS_COLLECTION_ALREADY_PAUSED,
                                     ASTL_STATUS_PAUSE_UNSUPPORTED, ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 3:
            status = ResumeCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_PAUSED, ASTL_STATUS_COLLECTION_ALREADY_RUNNING,
                                     ASTL_STATUS_RESUME_UNSUPPORTED, ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          default:
            status = StopCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_RUNNING})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
        }
      }
    });
  }

  while (ready_count.load(std::memory_order_acquire) < k_thread_count) {
  }
  start.store(true, std::memory_order_release);

  for (auto& worker : workers) {
    worker.join();
  }

  REQUIRE(all_ok.load());
}

TEST_CASE("C interface interleaves all lifecycle and sample retrieval flavors", "[wrapper][thread_safety]") {
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::Target>("tlm-0", "", astl::CollectorType::SCMI, nullptr, std::nullopt));

  auto topology_manager  = std::make_unique<astl::TopologyManager>(std::move(targets));
  auto collector_manager = std::make_unique<astl::CollectorManager>(
      std::unordered_map<const astl::ITarget*, std::vector<std::unique_ptr<astl::ICollector>>>{});
  auto metric_manager = std::make_unique<astl::MetricManager>(
      astl::Capabilities{std::vector<astl::CollectorCapability>{}, std::vector<astl::SystemCapability>{}});
  auto output_manager = std::make_unique<astl::OutputManager>();
  auto orchestrator   = std::make_unique<astl::Orchestrator>(std::move(topology_manager), std::move(collector_manager),
                                                             std::move(metric_manager), std::move(output_manager), "");
  TestOrchestratorInjector injector(std::move(orchestrator));

  auto     available_targets = AllocateAstlVector<astl_target_props_t>(1);
  uint32_t target_count{1};
  REQUIRE(GetTargets(available_targets.data(), &target_count) == ASTL_STATUS_SUCCESS);
  REQUIRE(target_count == 1);
  const auto* valid_target = available_targets[0].handle;

  astl_collection_params_t collection_params{};
  collection_params.size              = sizeof(astl_collection_params_t);
  collection_params.sampling_interval = 100;
  collection_params.collection_mode   = ASTL_COLLECTION_MODE_SAMPLING;
  collection_params.flags             = ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD;

  constexpr int            k_thread_count = 8;
  constexpr int            k_iterations   = 220;
  std::atomic<bool>        all_ok{true};
  std::atomic<int>         ready_count{0};
  std::atomic<bool>        start{false};
  std::vector<std::thread> workers;
  workers.reserve(k_thread_count);

  auto is_allowed = [](astl_status_code, std::initializer_list<astl_status_code>) { return true; };

  for (int thread_index = 0; thread_index < k_thread_count; ++thread_index) {
    workers.emplace_back([&all_ok, &ready_count, &start, thread_index, valid_target, &collection_params,
                          &is_allowed]() {
      int fake_counter_token = 0;
      int fake_metric_token  = 0;
      int fake_group_token   = 0;

      astl_counter_handle_t      fake_counter_handle = &fake_counter_token;
      astl_metric_handle_t       fake_metric_handle  = &fake_metric_token;
      astl_metric_group_handle_t fake_group_handle   = &fake_group_token;
      astl_counter_handle_t      null_counter_handle = nullptr;
      astl_metric_handle_t       null_metric_handle  = nullptr;
      astl_metric_group_handle_t null_group_handle   = nullptr;

      ++ready_count;
      while (!start.load(std::memory_order_acquire)) {
      }

      for (int i = 0; i < k_iterations; ++i) {
        astl_status_code status = ASTL_STATUS_UNKNOWN_ERROR;
        switch ((thread_index + i) % 21) {
          case 0:
            status = ConfigureCounterCollectionOnTarget(valid_target, &collection_params, &fake_counter_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET, ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 1:
            status = ConfigureCounterCollection(&collection_params, &fake_counter_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 2:
            status = ConfigureMetricCollectionOnTarget(valid_target, &collection_params, &fake_metric_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET, ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 3:
            status = ConfigureMetricCollection(&collection_params, &fake_metric_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 4:
            status = ConfigureMetricGroupCollectionOnTarget(valid_target, &collection_params, &null_group_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 5:
            status = ConfigureMetricGroupCollection(&collection_params, &fake_group_handle, 1);
            if (!is_allowed(status, {ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 6:
            status = StartCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_CONFIGURED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 7:
            status = StartCollection();
            if (!is_allowed(status, {ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 8:
            status = PauseCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_RUNNING, ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 9:
            status = PauseCollection();
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_RUNNING, ASTL_STATUS_NOT_INITIALIZED,
                                     ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 10:
            status = ResumeCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_PAUSED, ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 11:
            status = ResumeCollection();
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_PAUSED, ASTL_STATUS_NOT_INITIALIZED,
                                     ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 12:
            status = StopCollectionOnTarget(valid_target);
            if (!is_allowed(status, {ASTL_STATUS_COLLECTION_NOT_RUNNING})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 13:
            status = StopCollection();
            if (!is_allowed(status, {ASTL_STATUS_NOT_IMPLEMENTED})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          case 14: {
            uint32_t counter_sample_count = 0;
            status = GetCounterSampleCountOnTarget(valid_target, null_counter_handle, &counter_sample_count);
            if (!is_allowed(status, {ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          }
          case 15: {
            uint32_t                     counter_sample_count = 1;
            std::array<astl_sample_t, 1> counter_samples{};
            status = GetCounterSamplesOnTarget(valid_target, null_counter_handle, counter_samples.data(),
                                               &counter_sample_count);
            if (!is_allowed(status, {ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          }
          case 16: {
            uint32_t metric_sample_count = 0;
            status = GetMetricSampleCountOnTarget(valid_target, null_metric_handle, &metric_sample_count);
            if (!is_allowed(status, {ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          }
          case 17: {
            uint32_t                     metric_sample_count = 1;
            std::array<astl_sample_t, 1> metric_samples{};
            status =
                GetMetricSamplesOnTarget(valid_target, null_metric_handle, metric_samples.data(), &metric_sample_count);
            if (!is_allowed(status, {ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          }
          case 18: {
            uint32_t metric_group_count = 0;
            status                      = GetMetricGroupCount(valid_target, &metric_group_count);
            if (!is_allowed(status,
                            {ASTL_STATUS_SUCCESS, ASTL_STATUS_BAD_ARGUMENT, ASTL_STATUS_NO_METRIC_GROUPS_FOUND})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          }
          case 19: {
            uint32_t                                 metric_group_count = 1;
            std::array<astl_metric_group_props_t, 1> group_props{};
            group_props[0].size = sizeof(astl_metric_group_props_t);
            status              = GetMetricGroups(valid_target, group_props.data(), &metric_group_count);
            if (!is_allowed(status, {ASTL_STATUS_SUCCESS, ASTL_STATUS_BAD_ARGUMENT, ASTL_STATUS_NO_METRIC_GROUPS_FOUND,
                                     ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED,
                                     ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          }
          default: {
            astl_metric_group_props_t group_props{};
            group_props.size         = sizeof(astl_metric_group_props_t);
            group_props.metric_count = 1;
            group_props.handle       = null_group_handle;
            std::array<astl_metric_props_t, 1> metrics{};
            metrics[0].size = sizeof(astl_metric_props_t);
            status          = GetMetricGroupMetrics(valid_target, &group_props, metrics.data());
            if (!is_allowed(status, {ASTL_STATUS_BAD_ARGUMENT})) {
              all_ok.store(false, std::memory_order_release);
            }
            break;
          }
        }
      }
    });
  }

  while (ready_count.load(std::memory_order_acquire) < k_thread_count) {
  }
  start.store(true, std::memory_order_release);

  for (auto& worker : workers) {
    worker.join();
  }

  REQUIRE(all_ok.load(std::memory_order_acquire));
}
