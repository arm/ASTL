#ifndef ASTL_API_IMPL_HPP_
#define ASTL_API_IMPL_HPP_

#include <memory>

#include "astl/astl.h"
#include "collector/i_collector_manager.hpp"
#include "common/i_sample_sink.hpp"
#include "metric/i_metric_manager.hpp"
#include "target.hpp"
#include "topology/i_topology_manager.hpp"

static_assert(sizeof(astl_value_t) == sizeof(double),
              "astl_value_t union should not change size for ABI compatibility");

namespace astl {

class Orchestrator : public ISampleSink {
 public:
  /**
   * @brief Create a fully armed and operational Orchestrator from the necessary parts.
   *        One of Orchestrator's class invariants is that it has non-null topology, collector, and metric managers.
   *
   * @param topology_manager - Used to discover the hardware components (targets) on the current platform.
   *
   * @param collector_manager - Can be given a set of operations and hints on how to run them,
   *                            and then sample the data on an appropriate data source
   *
   * @param metric_manager - Can turn a set of desired metrics into a set of operations to collect,
   *                         then post-process the sampled data
   */
  Orchestrator(std::unique_ptr<ITopologyManager> topology_manager, std::unique_ptr<ICollectorManager> collector_manager,
               std::unique_ptr<IMetricManager> metric_manager);

  ~Orchestrator() override;

  // forbid copy
  Orchestrator(Orchestrator const &)            = delete;
  Orchestrator &operator=(Orchestrator const &) = delete;
  // forbid move construction for now
  // (if you add them later, be sure to move handle the _collector_manager's sample-sink registration)
  Orchestrator(Orchestrator &&other)            = delete;
  Orchestrator &operator=(Orchestrator &&other) = delete;

  /**
   * @brief Initialize the static singleton instance of Orchestrator, to be retrieved later through GetInstance
   *
   * @param topology_manager - Used to discover the hardware components (targets) on the current platform.
   *
   * @param collector_manager - Can be given a set of operations and hints on how to run them,
   *                            and then sample the data on an appropriate data source
   *
   * @param metric_manager - Can turn a set of desired metrics into a set of operations to collect,
   *                         then post-process the sampled data
   */
  static void InitializeInstance(std::unique_ptr<ITopologyManager>  topology_manager,
                                 std::unique_ptr<ICollectorManager> collector_manager,
                                 std::unique_ptr<IMetricManager>    metric_manager);

  /**
   * @brief Return a reference to the single Orchestrator instance
   *        If one hasn't been constructed yet, a default one with no collectors,
   *        metrics, or targets will be created in a thread-safe way.
   *        astlInitialize will use this returned reference to assign a new Orchestrator that may
   *        have more complex internals
   *
   * @return a reference to an owning pointer to Orchestrator. Will return nullptr before InitializeInstance is called
   */
  static std::unique_ptr<Orchestrator> &GetInstance();

  /**
   * @brief Returns a const reference to the set of Targets managed by this orchestrator.
   */
  std::vector<std::unique_ptr<ITarget>> const &GetTargets() const;

  /**
   * @brief Reassign the set of Targets managed by this orchestrator.
   *
   * Refactor - We probably want to provide a more controlled interface for modifying the target list
   *  For example, we could add member functions to enable/disable specific targets or
   *  modify the list internally when we read the configuration.
   */
  astl_status_code SetTargets(std::vector<std::unique_ptr<ITarget>> new_targets);

  /**
   * @brief For a given target, enable collection on a set of measurable Counters.
   *
   * @param target The target from which the collection will be sampled
   * @param collection_params Specifies how the collection should be gathered
   * @param counters The set of data points to collect
   *
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET: one of the given counters is not associated with the target
   */
  astl_status_code ConfigureCounterCollection(ITarget *target, const astl_collection_parameters_t *collection_params,
                                              std::span<ICounter *> counters);

  /**
   * @brief For a given target, enable collection on a set of measurable Metrics.
   *
   * @param target The target from which the collection will be sampled
   * @param collection_params Specifies how the collection should be gathered
   * @param metrics The set of data points to collect
   *
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET: one of the given metrics is not associated with the target
   */
  astl_status_code ConfigureMetricCollection(ITarget *target, const astl_collection_parameters_t *collection_params,
                                             std::span<const astl_metric_handle_t> metrics);

  /**
   * @brief Apply the previously configured collection on the given target
   *
   * Attempts to enable any data sources set up by ConfigureCounterCollection or similar, and may take initial sample
   * @param target The target with an active collection configuration
   * @note ConfigureCounterCollection or similar should be called first
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  astl_status_code StartCollection(ITarget *target);

  /**
   * @brief Collect one sample of data on a target with an active configured collection
   *
   * @param target The target with an active collection configuration
   * @note ConfigureCounterCollection or similar should be called before ReadImmediate
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  astl_status_code ReadImmediate(ITarget *target);

  /**
   * @brief Stop the collection of samples, but leave configuration in place
   *
   * @param target The target with an active collection configuration
   * @note StartCollection should be called before this
   * @note Re-enable collection with ResumeCollection
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  astl_status_code PauseCollection(ITarget *target);

  /**
   * @brief Re-enable the collection of samples, based on previous configuration
   *
   * @param target The target with an active collection configuration
   * @note PauseCollection should be called before this
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  astl_status_code ResumeCollection(ITarget *target);

  /**
   * @brief Stop the collection of samples
   *
   * This stops collecting samples, restores original system configuration (disabling data sources),
   * and captures any final samples necessary.
   *
   * @param target The target with an active collection configuration
   * @note StartCollection should be called before this
   * @note To re-enable collection, StartCollection should be sufficient.
   * @return error status code:
   *   - ASTL_STATUS_SUCCESS: success
   *   - ASTL_STATUS_INVALID_TARGET_HANDLE: the given target is unrecognized
   *   - others: according to individual Collector implementations
   */
  astl_status_code StopCollection(ITarget *target);

  /**
   * @brief Return the number of collected samples for a given counter on the given target
   * @param target The target on which collection was configured and performed
   * @param counter The specific data source that was sampled
   *
   * @return a std::expected pair with either:
   *   - a value: the count of samples taken for the given ICounter on the target
   *   - OR an error status code such as an invalid handle or bad argument
   */
  std::expected<uint32_t, astl_status_code> GetCounterSampleCount(const ITarget *target, const ICounter *counter) const;

  std::span<const SampledData> GetSamples() const { return _samples; }

  // TODO(ASTL-58): when OutputManager is implemented, revisit to see if GetMetricManager is even needed
  /**
   * @brief Return a reference to a pointer to the MetricManager, used to enumerate metrics
   */
  const std::unique_ptr<IMetricManager> &GetMetricManager() const { return _metric_manager; }

  /**
   * @brief Implementation of the ISampleSink interface - Receives samples from CollectorManager
   */
  astl_status_code SinkSamples(ITarget *target, std::span<SampledData> samples) override;

 private:
  static std::mutex                 &GetMutex();  // manange thread-safe access to singleton instance
  std::unique_ptr<ITopologyManager>  _topology_manager;
  std::unique_ptr<ICollectorManager> _collector_manager;
  std::unique_ptr<IMetricManager>    _metric_manager;
  std::vector<SampledData>           _samples;
};

}  // namespace astl

#endif  // ASTL_API_IMPL_HPP_
