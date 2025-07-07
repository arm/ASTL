#include "astl_impl.hpp"

#include "astl/astl_errors.h"
#include "astl_logger.hpp"

namespace astl {

std::vector<std::unique_ptr<ITarget>> const &Orchestrator::GetTargets() const { return _targets; }

void Orchestrator::SetTargets(std::vector<std::unique_ptr<ITarget>> targets) { _targets = std::move(targets); }

astl_status_code Orchestrator::ConfigureCounterCollection(ITarget                            *target,
                                                          const astl_collection_parameters_t *collection_params,
                                                          std::span<ICounter *>               counters) {
  auto index = std::find_if(std::begin(_targets), std::end(_targets),
                            [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(_targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  // unused, since unimplemented
  (void)collection_params;
  (void)counters;
  return ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET;
}

astl_status_code Orchestrator::StartCollection(ITarget *target) {
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::StartCollection called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto index = std::find_if(std::begin(_targets), std::end(_targets),
                            [target](auto const &owned_target) { return owned_target.get() == target; });

  if (index == std::end(_targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }

  _samples.clear();
  return _collector_manager->StartOnTarget(target);
}

astl_status_code Orchestrator::ReadImmediate(ITarget *target) {
  if (!_collector_manager) {
    ASTL_LOG_ERROR("Orchestrator::ReadImmediate called with null CollectorManager");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return _collector_manager->ReadImmediateOnTarget(target);
}

astl_status_code Orchestrator::PauseCollection(ITarget *target) {
  auto index = std::find_if(std::begin(_targets), std::end(_targets),
                            [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(_targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  return ASTL_STATUS_NOT_IMPLEMENTED;
}

astl_status_code Orchestrator::ResumeCollection(ITarget *target) {
  auto index = std::find_if(std::begin(_targets), std::end(_targets),
                            [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(_targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }
  return ASTL_STATUS_NOT_IMPLEMENTED;
}

astl_status_code Orchestrator::StopCollection(ITarget *target) {
  if (!_metric_manager) {
    ASTL_LOG_ERROR("null _metric_manager in Orchestrator::StopCollection");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_collector_manager) {
    ASTL_LOG_ERROR("null _collector_manager in Orchestrator::StopCollection");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  auto index = std::find_if(std::begin(_targets), std::end(_targets),
                            [target](auto const &owned_target) { return owned_target.get() == target; });
  if (index == std::end(_targets)) {
    return ASTL_STATUS_INVALID_TARGET_HANDLE;
  }

  astl_status_code status = _metric_manager->ProcessData(_samples);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  status = _metric_manager->SummarizeMetrics();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  status = _collector_manager->StopOnTarget(target);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  return ASTL_STATUS_SUCCESS;
}

std::expected<uint32_t, astl_status_code> Orchestrator::GetCounterSampleCount(const ITarget  *target,
                                                                              const ICounter *counter) const {
  auto index = std::find_if(std::begin(_targets), std::end(_targets),
                            [target](auto const &owned_target) { return owned_target.get() == target; });

  if (index == std::end(_targets)) {
    return std::unexpected(ASTL_STATUS_INVALID_TARGET_HANDLE);
  }
  (void)counter;  // unused since unimplemented for now
  return std::unexpected(ASTL_STATUS_INVALID_COUNTER_HANDLE);
}

astl_status_code Orchestrator::SinkSamples(ITarget *target, std::span<SampledData> samples) {
  if (!target) {
    ASTL_LOG_ERROR("Orchestrator::SinkSamples called with null target");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  /* Get the target name - just for logging */
  astl_target_properties_t properties;
  auto                     result = target->GetProperties(&properties);
  if (result != ASTL_STATUS_SUCCESS) {
    return result;
  }
  ASTL_LOG_DEBUG("Received {} samples for target {}", samples.size(), properties._name);
  for (const auto &sample : samples) {
    _samples.push_back(sample);
    auto timestamp_ns = sample.timestamp.time_since_epoch().count();
    auto value        = sample.value;

    ASTL_LOG_DEBUG("Sample - timestamp (ns since epoch): {}, value: {}", timestamp_ns, value);
  }

  return ASTL_STATUS_SUCCESS;
}

astl_status_code Orchestrator::Test() {
  ASTL_LOG_INFO("Test method is deprecated: {:d}", static_cast<uint32_t>(ASTL_STATUS_DEPRECATED_API));
  return ASTL_STATUS_DEPRECATED_API;
}

}  // namespace astl
