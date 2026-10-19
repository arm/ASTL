// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "windows/windows_system_collector.hpp"

#include <algorithm>
#include <vector>

#include "astl_logger.hpp"

namespace astl {

WindowsSystemCollector::WindowsSystemCollector(std::unique_ptr<IWindowsSystemApi> api) : _api(std::move(api)) {}

WindowsSystemCollector::~WindowsSystemCollector() { StopIntervalSampling(); }

auto WindowsSystemCollector::GetCapabilities() const -> CollectorCapability { return _collector_capability; }

auto WindowsSystemCollector::SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void {
  std::scoped_lock lock{_collection_mutex};
  _sample_sink = raw_sample_sink;
}

auto WindowsSystemCollector::ConfigureCollection(CollectionConfiguration&& configuration) -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::CONFIGURED &&
      _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  std::vector<std::string> instances;
  const auto&              phases = configuration.Operations();
  for (const auto* phase : {&phases.operationsBeforeStart, &phases.operationsAtStart, &phases.operationsOnSample,
                            &phases.operationsAtStop}) {
    for (const auto& operation : *phase) {
      const auto* read = dynamic_cast<const WindowsReadOperation*>(operation.get());
      if (!read) {
        return ASTL_STATUS_BAD_ARGUMENT;
      }
      if (read->field == WindowsSystemField::CPU_UTILIZATION &&
          std::find(instances.begin(), instances.end(), read->instance) == instances.end()) {
        instances.push_back(read->instance);
      }
    }
  }
  if (const auto status = _api->ConfigureCpu(instances); status != ASTL_STATUS_SUCCESS) {
    _configuration.reset();
    _collection_state = CollectionState::UNCONFIGURED;
    return status;
  }
  _configuration    = std::move(configuration);
  _collection_state = CollectionState::CONFIGURED;
  return ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
}

auto WindowsSystemCollector::ClearCollectionState() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STARTED || _collection_state == CollectionState::PAUSED) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  if (const auto status = _api->ConfigureCpu({}); status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  _configuration.reset();
  _collection_state = CollectionState::UNCONFIGURED;
  return ASTL_STATUS_SUCCESS;
}

auto WindowsSystemCollector::StartCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (const auto status = CheckCollectionLifecycleAction(_collection_state, CollectionLifecycleAction::START);
      status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (!_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  if (const auto status = _api->ResetCpu(); status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  auto result = ExecuteStartModeOperations();
  if (result == ASTL_STATUS_SUCCESS) {
    _collection_state = CollectionState::STARTED;
  }
  return result;
}

auto WindowsSystemCollector::PauseCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (const auto status = CheckCollectionLifecycleAction(_collection_state, CollectionLifecycleAction::PAUSE);
      status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (_periodic_sampler) {
    _periodic_sampler->Pause();
  } else {
    ASTL_LOG_WARNING("WindowsSystemCollector: PauseCollection called without an active periodic sampler");
  }
  _collection_state = CollectionState::PAUSED;
  return ASTL_STATUS_SUCCESS;
}

auto WindowsSystemCollector::ResumeCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (const auto status = CheckCollectionLifecycleAction(_collection_state, CollectionLifecycleAction::RESUME);
      status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (const auto status = _api->ResetCpu(); status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (_periodic_sampler) {
    _periodic_sampler->Resume();
  } else {
    ASTL_LOG_WARNING("WindowsSystemCollector: ResumeCollection called without an active periodic sampler");
  }
  _collection_state = CollectionState::STARTED;
  return ASTL_STATUS_SUCCESS;
}

auto WindowsSystemCollector::StopCollection() -> astl_status_code {
  StopIntervalSampling();

  std::scoped_lock lock{_collection_mutex};
  if (const auto status = CheckCollectionLifecycleAction(_collection_state, CollectionLifecycleAction::STOP);
      status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (!_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }

  const auto result = ExecuteStopModeOperations();
  if (result == ASTL_STATUS_SUCCESS) {
    _collection_state = CollectionState::STOPPED;
    if (const auto status = _api->ResetCpu(); status != ASTL_STATUS_SUCCESS) {
      return status;
    }
  }
  return result;
}

auto WindowsSystemCollector::ReadImmediate() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::CONFIGURED &&
       _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  return ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
}

auto WindowsSystemCollector::ExecuteStartModeOperations() -> astl_status_code {
  auto status = ExecuteCollectionOperations(_configuration->Operations().operationsAtStart);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  switch (_configuration->CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      status = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      status = StartIntervalSampling();
      break;
    default:
      status = ASTL_STATUS_BAD_CONFIGURATION;
      break;
  }

  return status;
}

auto WindowsSystemCollector::ExecuteStopModeOperations() -> astl_status_code {
  astl_status_code status = ASTL_STATUS_SUCCESS;
  switch (_configuration->CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      status = ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      break;
    default:
      status = ASTL_STATUS_BAD_CONFIGURATION;
      break;
  }

  if (status == ASTL_STATUS_SUCCESS) {
    status = ExecuteCollectionOperations(_configuration->Operations().operationsAtStop);
  }
  return status;
}

auto WindowsSystemCollector::StartIntervalSampling() -> astl_status_code {
  if (_collection_state != CollectionState::CONFIGURED && _collection_state != CollectionState::STOPPED &&
      _collection_state != CollectionState::PAUSED) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (!_configuration.has_value()) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  if (_periodic_sampler) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto interval     = std::chrono::milliseconds{_configuration->CollectionParams().sampling_interval};
  _periodic_sampler = std::make_unique<PeriodicSampler>(this, interval);
  return ASTL_STATUS_SUCCESS;
}

auto WindowsSystemCollector::StopIntervalSampling() -> void { _periodic_sampler = nullptr; }

}  // namespace astl
