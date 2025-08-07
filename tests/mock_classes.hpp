#ifndef ASTL_MOCK_CLASSES_H_
#define ASTL_MOCK_CLASSES_H_

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "astl/astl_errors.h"
#include "collector/collection_configuration.hpp"
#include "collector/i_collector.hpp"
#include "collector/i_collector_manager.hpp"
#include "common/capabilities.hpp"
#include "common/i_sample_sink.hpp"
#include "common/operation.hpp"
#include "counter.hpp"
#include "metric/i_metric.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"
#include "test_includes.hpp"
#include "topology/i_topology_manager.hpp"

/**
 * @brief A mockable implementation of the astl::ITarget interface
 *
 * Set up expected function calls and their results and side effects with REQUIRE_CALL and ALLOW_CALL
 */
struct MockTarget : public astl::ITarget {
 private:
  std::vector<std::unique_ptr<astl::ICounter>> _counters;

 public:
  static constexpr bool trompeloeil_movable_mock = true;  // cppcheck-suppress unusedStructMember

  MockTarget() = default;
  explicit MockTarget(std::vector<std::unique_ptr<astl::ICounter>> counters) : _counters{std::move(counters)} {}

  MAKE_MOCK1(GetProperties, astl_status_code(astl_target_properties_t* target), override);
  MAKE_CONST_MOCK0(GetCounterCount, size_t(), override);
  std::vector<std::unique_ptr<astl::ICounter>> const& GetCounters() const override { return _counters; };
};

/**
 * @brief A mockable implementation of Orchestrator's telemetry interface
 *
 * Set up expected function calls and their results and side effects with REQUIRE_CALL and ALLOW_CALL
 */
struct MockOrchestrator {
 private:
 public:
  static constexpr bool trompeloeil_movable_mock = true;  // cppcheck-suppress unusedStructMember

  MAKE_MOCK3(ConfigureCounterCollection,
             astl_status_code(astl::ITarget* target, astl_collection_parameters_t const* const collection_params,
                              std::span<astl::ICounter*> counters));

  MAKE_MOCK1(ReadImmediate, astl_status_code(astl::ITarget* target));
  MAKE_MOCK1(StartCollection, astl_status_code(astl::ITarget* target));
  MAKE_MOCK1(PauseCollection, astl_status_code(astl::ITarget* target));
  MAKE_MOCK1(ResumeCollection, astl_status_code(astl::ITarget* target));
  MAKE_MOCK1(StopCollection, astl_status_code(astl::ITarget* target));
  using RType = std::expected<uint32_t, astl_status_code>;  // define this separately to avoid MACRO expansion quirk
  MAKE_CONST_MOCK2(GetCounterSampleCount, RType(astl::ITarget const* target, const astl::ICounter*));
};

/**
 * @brief A mockable implementation of the astl::ICounter interface
 *
 * Set up expected function calls and their results and side effects with REQUIRE_CALL and ALLOW_CALL
 */
struct MockCounter : public astl::ICounter {
  // clang-format off
  MAKE_MOCK1(GetProperties, auto(astl_counter_properties_t*) -> astl_status_code, override);
  MAKE_MOCK1(ConfigureCollection, auto(astl_collection_parameters_t const* const) -> astl_status_code, override);
  // clang-format on
};

// MockFileInterface is a mockable implementation of the astl::FileInterface
struct MockFileInterface {
  static constexpr bool trompeloeil_movable_mock = true;

  using expected_bool = std::expected<bool, astl_status_code>;
  MAKE_MOCK1(IsValid, auto(const std::filesystem::path&)->expected_bool, const noexcept);
  MAKE_MOCK1(HasReadPermission, auto(const std::filesystem::path&)->expected_bool, const noexcept);
  MAKE_MOCK1(HasWritePermission, auto(const std::filesystem::path&)->expected_bool, const noexcept);
  MAKE_MOCK2(Read, auto(const std::filesystem::path&, std::string&)->astl_status_code, const);
  MAKE_MOCK2(Write, auto(const std::filesystem::path&, const std::string_view)->astl_status_code, const);
};

struct MockTopologyManager : public astl::ITopologyManager {
  static constexpr bool trompeloeil_movable_mock = true;
  using InitializeCollectorManagerRtype =
      std::pair<std::vector<std::unique_ptr<astl::ITarget>>, std::unique_ptr<astl::ICollectorManager>>;
  using InitializeMetricManagerRtype = std::expected<std::unique_ptr<astl::IMetricManager>, astl_status_code>;
  MAKE_MOCK0(ScanForTargets, astl_status_code(), override);
  MAKE_CONST_MOCK1(InitializeMetricManager, InitializeMetricManagerRtype(const astl::AstlConfiguration& configuration),
                   override);
  const std::vector<std::unique_ptr<astl::ITarget>>& GetTargets() const override { return _targets; }
  astl_status_code SetTargets(std::vector<std::unique_ptr<astl::ITarget>> new_targets) override {
    _targets = std::move(new_targets);
    return ASTL_STATUS_SUCCESS;
  }

 private:
  std::vector<std::unique_ptr<astl::ITarget>> _targets;
};

struct MockCollectorManager : public astl::ICollectorManager {
  static constexpr bool trompeloeil_movable_mock = true;

  using CollectionCapabilitiesRtype = std::unordered_map<astl::ITarget*, std::vector<astl::CollectorCapability>>;
  MAKE_CONST_MOCK0(ReportCollectionCapabilities, CollectionCapabilitiesRtype(), override);
  MAKE_MOCK1(RegisterSampleSink, astl_status_code(astl::ISampleSink* sink), override);
  MAKE_MOCK1(UnregisterSampleSink, astl_status_code(astl::ISampleSink* sink), override);

  MAKE_MOCK3(ConfigureCollectionOnTarget,
             astl_status_code(astl::ITarget* target, astl_collection_parameters_t const& collection_params,
                              astl::CollectionOperations&& configuration),
             override);

  MAKE_MOCK1(StartOnTarget, astl_status_code(astl::ITarget* target), override);
  MAKE_MOCK1(PauseOnTarget, astl_status_code(astl::ITarget* target), override);
  MAKE_MOCK1(ResumeOnTarget, astl_status_code(astl::ITarget* target), override);
  MAKE_MOCK1(ReadImmediateOnTarget, astl_status_code(astl::ITarget* target), override);
  MAKE_MOCK1(StopOnTarget, astl_status_code(astl::ITarget* target), override);

 private:
};

struct MockCollector : public astl::ICollector {
  /* @brief Get the capabilities of this collector, including the collector type. */
  MAKE_MOCK0(GetCapabilities, astl::CollectorCapability const&(), const override);

  /*
   * @brief Set the destination for where sampled data should be sent.
   *       This is typically the CollectorManager, but can be any ISampleSink.
   */
  MAKE_MOCK1(SetSampleSink, void(astl::ISampleSink* sample_sink), override);

  /*
   * @brief Configure the collector to collect data, but don't start sampling it yet.
   *
   * @param configuration The configuration to apply to this collector, including the set of operations to run,
   *        the interval to sample at.
   */
  MAKE_MOCK1(ConfigureCollection, astl_status_code(astl::CollectionConfiguration&& configuration), override);

  /* @brief Start the collection of data, performing any setup operations, starting sampling async tasks, etc. */
  MAKE_MOCK0(StartCollection, astl_status_code(), override);

  /* @brief Pause the collection of data, stopping any async tasks, but keeping the configuration intact. */
  MAKE_MOCK0(PauseCollection, astl_status_code(), override);

  /*
   * @brief Resume the collection of data, starting any async tasks
   */
  MAKE_MOCK0(ResumeCollection, astl_status_code(), override);

  /*
   * @brief Stop the collection of data, performing any cleanup operations, stopping async tasks, etc.
   */
  MAKE_MOCK0(StopCollection, astl_status_code(), override);

  /*
   * @brief Collect a single sample of all the configured metics.
   */
  MAKE_MOCK0(ReadImmediate, astl_status_code(), override);
};

struct MockSampleSink : public astl::ISampleSink {
  /*
   * Deliver Some number of samples collected from the given target to this ISampleSink
   */
  MAKE_MOCK2(SinkSamples, astl_status_code(astl::ITarget* target, std::span<astl::SampledData> samples), override);
};

struct MockMetric : public astl::IMetric {
  using expected_operation_sequence = std::expected<astl::OperationSequence, astl_status_code>;

  MAKE_MOCK1(CheckCapabilities, auto(const astl::Capabilities& capabilities)->bool, const override);
  MAKE_MOCK0(GetOperations, auto()->expected_operation_sequence, const override);
  MAKE_MOCK1(ReceiveSample, auto(const astl::SampledData& sample)->astl_status_code, override);

  using samples_t = std::span<const astl::SampledData>;
  MAKE_MOCK0(GetSamples, auto()->samples_t, const override);
  MAKE_MOCK0(Reset, auto()->void, override);
  MAKE_MOCK0(Summarize, auto()->astl_status_code, override);
  MAKE_MOCK1(GetProperties, auto(astl_metric_properties_t* properties)->astl_status_code, const override);
};

struct MockMetricManager : public astl::IMetricManager {
  static constexpr bool trompeloeil_movable_mock = true;

  using expected_collection_operations = std::expected<astl::CollectionOperations, astl_status_code>;
  using expected_metric_interface      = std::expected<std::span<astl::IMetric* const>, astl_status_code>;

  /*
   * @brief Register a new metric with the metric manager.
   *
   * @param metric_config A unique pointer to a MetricConfig describing the metric to be registered.
   * @return astl_status_code indicating success or failure of the registration process.
   */
  MAKE_MOCK1(RegisterMetric, auto(std::unique_ptr<astl::MetricConfig>)->astl_status_code, override);

  /*
   * @brief Retrieve a list of all currently registered and available metrics.
   *
   * @return A std::expected containing either a span of IMetric pointers if successful,
   *         or an astl_status_code in case of error.
   */
  MAKE_MOCK0(GetAvailableMetrics, expected_metric_interface(), const override);

  /*
   * @brief Determine the required operations to support the specified metrics.
   *
   * @param metrics A span of metric pointers for which to determine the required operations.
   * @return A std::expected containing the required CollectionOperations struct if successful,
   *         or an astl_status_code on failure.
   */
  MAKE_MOCK1(GetRequiredOperations, auto(std::span<astl::IMetric* const>)->expected_collection_operations, override);

  /*
   * @brief Process the sampled data for all registered metrics.
   *
   * @param data A span of SampledData objects to process.
   * @return astl_status_code indicating success or failure of the processing operation.
   */
  MAKE_MOCK1(ProcessData, auto(std::span<astl::SampledData>)->astl_status_code, override);

  /*
   * @brief Perform a final summary or aggregation of all collected metric data.
   *
   * @return astl_status_code indicating success or failure of the summarization process.
   */
  MAKE_MOCK0(SummarizeMetrics, astl_status_code(), override);
};

#endif  // ASTL_MOCK_CLASSES_H_
