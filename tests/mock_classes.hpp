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
#include "common/i_processed_sample_sink.hpp"
#include "common/i_raw_sample_sink.hpp"
#include "counter.hpp"
#include "metric/i_metric.hpp"
#include "metric/i_metric_manager.hpp"
#include "operation/operation.hpp"
#include "output/i_output.hpp"
#include "output/i_output_manager.hpp"
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

  MAKE_MOCK0(GetCollectorType, auto()->astl::CollectorType, const override);
  MAKE_MOCK0(Name, auto()->std::string const&, const override);
  MAKE_MOCK1(GetProperties, auto(astl_target_properties_t* target)->astl_status_code, const override);
  MAKE_CONST_MOCK0(GetCounterCount, auto()->size_t, override);
  auto GetCounters() const -> std::vector<std::unique_ptr<astl::ICounter>> const& override { return _counters; };
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
             astl_status_code(const astl::ITarget* target, astl_collection_parameters_t const* const collection_params,
                              std::span<astl::ICounter*> counters));

  MAKE_MOCK1(ReadImmediate, astl_status_code(const astl::ITarget* target));
  MAKE_MOCK1(StartCollection, astl_status_code(const astl::ITarget* target));
  MAKE_MOCK1(PauseCollection, astl_status_code(const astl::ITarget* target));
  MAKE_MOCK1(ResumeCollection, astl_status_code(const astl::ITarget* target));
  MAKE_MOCK1(StopCollection, astl_status_code(const astl::ITarget* target));
  using RType = std::expected<uint32_t, astl_status_code>;  // define this separately to avoid MACRO expansion quirk
  MAKE_CONST_MOCK2(GetCounterSampleCount, RType(const astl::ITarget* target, const astl::ICounter*));
};

/**
 * @brief A mockable implementation of the astl::ICounter interface
 *
 * Set up expected function calls and their results and side effects with REQUIRE_CALL and ALLOW_CALL
 */
struct MockCounter : public astl::ICounter {
  // clang-format off
  MAKE_MOCK1(GetProperties, auto(astl_counter_properties_t*) -> astl_status_code, const override);
  MAKE_MOCK1(ConfigureCollection, auto(astl_collection_parameters_t const* const) -> astl_status_code, override);
  // clang-format on
};

// MockFileInterface is a mockable implementation of the astl::FileInterface
struct MockFileInterface {
  static constexpr bool trompeloeil_movable_mock = true;

  using expected_bool = std::expected<bool, astl_status_code>;
  MAKE_MOCK1(IsValid, auto(const std::filesystem::path&)->expected_bool, const noexcept);
  using expected_children = std::expected<std::vector<std::filesystem::directory_entry>, astl_status_code>;
  MAKE_MOCK0(GetSubdirectories, auto()->expected_children, const);
  MAKE_MOCK1(HasReadPermission, auto(const std::filesystem::path&)->expected_bool, const noexcept);
  MAKE_MOCK1(HasWritePermission, auto(const std::filesystem::path&)->expected_bool, const noexcept);
  MAKE_MOCK2(Read, auto(const std::filesystem::path&, std::string&)->astl_status_code, const);
  MAKE_MOCK2(Write, auto(const std::filesystem::path&, const std::string_view)->astl_status_code, const);
  MAKE_MOCK0(GetBasePath, auto()->const std::filesystem::path&, const);
};

struct MockTopologyManager : public astl::ITopologyManager {
  static constexpr bool trompeloeil_movable_mock = true;
  using InitializeCollectorManagerRtype =
      std::pair<std::vector<std::unique_ptr<astl::ITarget>>, std::unique_ptr<astl::ICollectorManager>>;
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

  using CollectionCapabilitiesRtype = std::unordered_map<const astl::ITarget*, std::vector<astl::CollectorCapability>>;
  MAKE_CONST_MOCK0(ReportCollectionCapabilities, CollectionCapabilitiesRtype(), override);
  MAKE_MOCK1(RegisterRawSampleSink, astl_status_code(astl::IRawSampleSink* sink), override);
  MAKE_MOCK1(UnregisterRawSampleSink, astl_status_code(astl::IRawSampleSink* sink), override);

  MAKE_MOCK3(ConfigureCollectionOnTarget,
             astl_status_code(const astl::ITarget* target, astl_collection_parameters_t const& collection_params,
                              astl::CollectionOperations&& configuration),
             override);

  MAKE_MOCK1(StartOnTarget, astl_status_code(const astl::ITarget* target), override);
  MAKE_MOCK1(PauseOnTarget, astl_status_code(const astl::ITarget* target), override);
  MAKE_MOCK1(ResumeOnTarget, astl_status_code(const astl::ITarget* target), override);
  MAKE_MOCK1(ReadImmediateOnTarget, astl_status_code(const astl::ITarget* target), override);
  MAKE_MOCK1(StopOnTarget, astl_status_code(const astl::ITarget* target), override);

 private:
};

struct MockCollector : public astl::ICollector {
  /* @brief Get the capabilities of this collector, including the collector type. */
  MAKE_MOCK0(GetCapabilities, astl::CollectorCapability const&(), const override);

  /*
   * @brief Set the destination for where sampled data should be sent.
   *       This is typically the CollectorManager, but can be any IRawSampleSink.
   */
  MAKE_MOCK1(SetRawSampleSink, void(astl::IRawSampleSink* raw_sample_sink), override);

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

struct MockRawSampleSink : public astl::IRawSampleSink {
  /*
   * Deliver Some number of samples collected from the given target to this IRawSampleSink
   */
  MAKE_MOCK2(SinkRawSamples, astl_status_code(const astl::ITarget* target, std::span<astl::RawSampledData> raw_samples),
             override);
};

struct MockProcessedSampleSink : public astl::IProcessedSampleSink {
  /*
   * Deliver some number of processed samples from the given target/metric to this sink.
   * Signature must match IProcessedSampleSink exactly (span<const ProcessedSampledData>).
   */
  MAKE_MOCK3(SinkProcessedSamples,
             auto(const astl::ITarget* target, const astl::IMetric* metric,
                  std::span<const astl::ProcessedSampledData> processed_samples)
                 ->astl_status_code,
             override);
};

struct MockMetric : public astl::IMetric {
  using expected_operation_sequence = std::expected<astl::OperationSequence, astl_status_code>;

  MAKE_MOCK1(CheckCapabilities, auto(const astl::Capabilities& capabilities)->bool, const override);
  MAKE_MOCK0(GetOperations, auto()->expected_operation_sequence, override);
  MAKE_MOCK1(ReceiveRawSample, auto(const astl::RawSampledData& raw_sample)->astl_status_code, override);
  MAKE_MOCK1(SetProcessedSampleSink, auto(astl::IProcessedSampleSink* sink)->void, final);
  MAKE_MOCK1(SinkProcessedSample, auto(astl::ProcessedSampledData const& processed_sample)->astl_status_code, override);

  using samples_t = std::span<const astl::ProcessedSampledData>;
  MAKE_MOCK0(Reset, auto()->void, override);
  MAKE_MOCK0(Summarize, auto()->astl_status_code, override);
  MAKE_MOCK1(GetProperties, auto(astl_metric_properties_t* properties)->astl_status_code, const override);
  MAKE_MOCK0(Name, auto()->std::string const&, const override);
};

struct MockMetricManager : public astl::IMetricManager {
  static constexpr bool trompeloeil_movable_mock = true;

  using expected_collection_operations = std::expected<astl::CollectionOperations, astl_status_code>;
  using expected_metric_interface      = std::expected<std::span<const astl_metric_handle_t>, astl_status_code>;
  using metric_expected_t              = std::expected<astl::IMetric*, astl_status_code>;

  // New in interface: map a metric API handle + target to an IMetric implementation
  MAKE_MOCK2(GetMetricOnTarget, metric_expected_t(astl_metric_handle_t metric_handle, const astl::ITarget* target),
             override);

  MAKE_MOCK1(RegisterProcessedSampleSink, astl_status_code(astl::IProcessedSampleSink* sink), override);
  MAKE_MOCK1(UnregisterProcessedSampleSink, astl_status_code(astl::IProcessedSampleSink* sink), override);

  /*
   * @brief Register a new metric with the metric manager.
   *
   * @param metric_config A unique pointer to a MetricConfig describing the metric to be registered.
   * @return astl_status_code indicating success or failure of the registration process.
   */
  MAKE_MOCK2(RegisterMetric,
             auto(std::unique_ptr<astl::MetricConfig>, std::vector<const astl::ITarget*> const&)->astl_status_code,
             override);
  /*
   * @brief Retrieve a list of all currently registered and available metrics.
   *
   * @return A std::expected containing either a span of IMetric pointers if successful,
   *         or an astl_status_code in case of error.
   */
  MAKE_MOCK0(GetAvailableMetrics, expected_metric_interface(), const override);

  /**
   * @brief Retrieve all registered metrics.
   * @param target The target from which to retrieve associated metrics
   * @return expected containing a span of registered astl_metric_handle_t on success,
   *         or an error code if retrieval fails.
   */
  MAKE_MOCK1(GetAvailableMetrics, auto(const astl::ITarget* target)->expected_metric_interface, const override);

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  MAKE_MOCK2(GetProperties, auto(const astl_metric_handle_t, astl_metric_properties_t*)->astl_status_code,
             const override);

  /*
   * @brief Determine the required operations to support the specified metrics.
   *
   * @param metrics A span of metric pointers for which to determine the required operations.
   * @return A std::expected containing the required CollectionOperations struct if successful,
   *         or an astl_status_code on failure.
   */
  MAKE_MOCK2(GetRequiredOperations,
             auto(std::span<const astl_metric_handle_t>, const astl::ITarget*)->expected_collection_operations,
             override);

  /*
   * @brief Process the sampled data for all registered metrics.
   *
   * @param data A span of RawSampledData objects to process.
   * @return astl_status_code indicating success or failure of the processing operation.
   */
  MAKE_MOCK1(ProcessRawSamples, auto(astl::RawSamplesMap&)->astl_status_code, override);

  MAKE_MOCK2(SinkProcessedSamples,
             auto(const astl::IMetric* metric, std::span<const astl::ProcessedSampledData> processed_samples)
                 ->astl_status_code,
             override);

  // NOTE: The GetProcessedSamples(metric_handle, target) method was removed from IMetricManager.
  // Tests should obtain processed samples via Orchestrator::GetProcessedMetricSamples after sinking them with
  // Orchestrator::SinkProcessedSamples. If legacy expectations are still present they should be updated.

  /*
   * @brief Perform a final summary or aggregation of all collected metric data.
   *
   * @return astl_status_code indicating success or failure of the summarization process.
   */
  MAKE_MOCK0(SummarizeMetrics, astl_status_code(), override);
};

struct MockOutput : public astl::IOutput {
  static constexpr bool trompeloeil_movable_mock = true;
};

struct MockOutputManager : public astl::IOutputManager {
  static constexpr bool trompeloeil_movable_mock = true;

  MAKE_MOCK2(CreateBufferOutput,
             astl_status_code(std::span<astl_metric_sample_t> samples_buffer, uint32_t* buffer_sample_count), override);
  MAKE_MOCK0(DestroyBufferOutput, astl_status_code(), override);
  MAKE_MOCK4(OutputProcessedSamples,
             astl_status_code(const astl::ProcessedSamplesMap& processed_samples, astl::OutputType output_type,
                              const astl::ITarget* target, const astl::IMetric* metric),
             override);
};

// MockSampleSink captures processed samples for test assertions
struct MockSampleSink : public astl::IProcessedSampleSink {
  std::vector<astl::ProcessedSampledData> captured;
  astl_status_code                        SinkProcessedSamples(const astl::ITarget* target, const astl::IMetric* metric,
                                                               std::span<const astl::ProcessedSampledData> samples) override {
    (void)target;
    (void)metric;
    captured.insert(captured.end(), samples.begin(), samples.end());
    return ASTL_STATUS_SUCCESS;
  }
};

#endif  // ASTL_MOCK_CLASSES_H_
