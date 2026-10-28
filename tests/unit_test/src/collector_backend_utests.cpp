// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>

#include "../../test_includes.hpp"
#include "collector/collector_builder.hpp"
#include "collector/i_collector.hpp"
#include "metric/metric_builder.hpp"
#include "topology/topology_builder.hpp"

namespace {
constexpr auto kTestType = static_cast<astl::CollectorType>(99);
struct TestBuilder {
  static auto BuildOperations(const astl::ITarget* /*target*/)
      -> std::expected<astl::OperationSequence, astl_status_code> {
    astl::OperationSequence operations;
    operations.push_back(std::make_unique<astl::Operation>());
    return operations;
  }
};
class TestCollector : public astl::ICollector {
 public:
  auto GetCapabilities() const -> astl::CollectorCapability override { return astl::CollectorCapability{kTestType}; }
  auto SetRawSampleSink(astl::IRawSampleSink* /*sink*/) -> void override {}
  auto ConfigureCollection(astl::CollectionConfiguration&& config) -> astl_status_code override {
    configured.emplace(std::move(config));
    return ASTL_STATUS_SUCCESS;
  }
  auto ClearCollectionState() -> astl_status_code override {
    configured.reset();
    return ASTL_STATUS_SUCCESS;
  }
  auto StartCollection() -> astl_status_code override {
    return configured ? ASTL_STATUS_SUCCESS : ASTL_STATUS_BAD_CONFIGURATION;
  }
  auto PauseCollection() -> astl_status_code override { return ASTL_STATUS_SUCCESS; }
  auto ResumeCollection() -> astl_status_code override { return ASTL_STATUS_SUCCESS; }
  auto StopCollection() -> astl_status_code override { return ASTL_STATUS_SUCCESS; }
  auto ReadImmediate() -> astl_status_code override { return StartCollection(); }
  auto GetNativeClockSnapshot() -> std::expected<astl::ClockCorrelationMap, astl_status_code> override {
    return astl::ClockCorrelationMap{};
  }

 private:
  std::optional<astl::CollectionConfiguration> configured;
};
auto Discover(const astl::AstlConfiguration& /*configuration*/)
    -> std::expected<astl::BackendTargets, astl_status_code> {
  astl::BackendTargets targets;
  targets.push_back(std::make_unique<astl::Target>("test-backend", "Test", kTestType));
  targets.push_back(std::make_unique<astl::Target>("test-backend", "Duplicate", kTestType));
  return targets;
}
auto Create(const astl::ITarget& /*target*/, const astl::AstlConfiguration& /*configuration*/)
    -> std::expected<std::unique_ptr<astl::ICollector>, astl_status_code> {
  return std::make_unique<TestCollector>();
}
auto Register(const astl::AstlConfiguration& /*configuration*/, const astl::BackendTargetMap& targets,
              astl::IMetricManager* manager) -> astl_status_code {
  return manager->RegisterMetric(
      std::make_unique<astl::MetricConfig>("test-value", "Test metric", ASTL_UNITS_COUNT, ASTL_VALUE_UINT64,
                                           ASTL_METRIC_IDENTIFIER_UNKNOWN, ASTL_METRIC_VALUE, kTestType, TestBuilder{}),
      targets.at(kTestType));
}
const astl::CollectorBackend kTestBackend{kTestType, "test", Discover, Create, Register};
auto                         Configuration() -> astl::AstlConfiguration {
  auto result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(result);
  auto config               = std::move(*result);
  config.collectors.enabled = std::vector{kTestType};
  return config;
}
}  // namespace

TEST_CASE("An independently registered backend works through common builders", "[CollectorBackend]") {
  const std::array backends{kTestBackend};
  const auto       config   = Configuration();
  auto             topology = astl::BuildTopologyManager(config, std::nullopt, backends);
  REQUIRE(topology);
  const auto& targets = (*topology)->GetTargets();
  REQUIRE(targets.size() == 1);  // Shared duplicate-name policy also applies to injected backends.
  auto collectors = astl::BuildCollectorManager(targets, config, backends);
  REQUIRE(collectors);
  auto metrics = astl::BuildMetricManager(targets, config, std::nullopt, backends);
  REQUIRE(metrics);
  auto handles = (*metrics)->GetAvailableMetrics(targets.front().get());
  REQUIRE(handles);
  REQUIRE(handles->size() == 1);
  auto operations = (*metrics)->GetRequiredOperations(*handles, targets.front().get());
  REQUIRE(operations);
  REQUIRE(operations->operationsOnSample.size() == 1);
  CHECK((*collectors)->ConfigureCollectionOnTarget(targets.front().get(), {}, std::move(*operations)) ==
        ASTL_STATUS_SUCCESS);
  CHECK((*collectors)->StartOnTarget(targets.front().get()) == ASTL_STATUS_SUCCESS);
  CHECK((*collectors)->ReadImmediateOnTarget(targets.front().get()) == ASTL_STATUS_SUCCESS);
  CHECK((*collectors)->StopOnTarget(targets.front().get()) == ASTL_STATUS_SUCCESS);
}

TEST_CASE("Backend registry rejects invalid registration and target ownership", "[CollectorBackend]") {
  auto config  = Configuration();
  auto backend = kTestBackend;
  SECTION("duplicate ID") {
    auto other = backend;
    other.name = "other";
    CHECK(astl::ValidateCollectorBackends(std::array{backend, other}) == ASTL_STATUS_BAD_CONFIGURATION);
  }
  SECTION("duplicate name") {
    auto other = backend;
    other.type = astl::CollectorType::SCMI;
    CHECK(astl::ValidateCollectorBackends(std::array{backend, other}) == ASTL_STATUS_BAD_CONFIGURATION);
  }
  SECTION("incomplete live backend") {
    backend.create = nullptr;
    CHECK(astl::ValidateCollectorBackends(std::array{backend}) == ASTL_STATUS_BAD_CONFIGURATION);
  }
  SECTION("discovery failure") {
    backend.discover =
        [](const astl::AstlConfiguration& /*configuration*/) -> std::expected<astl::BackendTargets, astl_status_code> {
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    };
    CHECK(astl::BuildTopologyManager(config, std::nullopt, std::array{backend}).error() == ASTL_STATUS_INTERNAL_ERROR);
  }
  SECTION("wrong owner") {
    backend.type              = astl::CollectorType::SCMI;
    config.collectors.enabled = std::vector{astl::CollectorType::SCMI};
    CHECK(astl::BuildTopologyManager(config, std::nullopt, std::array{backend}).error() ==
          ASTL_STATUS_BAD_CONFIGURATION);
  }
  SECTION("disabled backend") {
    config.collectors.enabled = std::vector<astl::CollectorType>{};
    auto topology             = astl::BuildTopologyManager(config, std::nullopt, std::array{backend});
    REQUIRE(topology);
    CHECK((*topology)->GetTargets().empty());
  }
  SECTION("missing collector backend") {
    auto targets = Discover(config);
    REQUIRE(targets);
    CHECK(astl::BuildCollectorManager(*targets, config, {}).error() == ASTL_STATUS_NOT_SUPPORTED);
  }
  SECTION("compiled-out backend skips discovery") {
    backend.discover = nullptr;
    auto topology    = astl::BuildTopologyManager(config, std::nullopt, std::array{backend});
    REQUIRE(topology);
    CHECK((*topology)->GetTargets().empty());
  }
}
