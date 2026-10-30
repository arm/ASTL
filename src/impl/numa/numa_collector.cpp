// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "numa/numa_collector.hpp"

#include <algorithm>
#include <vector>

#include "astl_logger.hpp"

namespace astl {

NumaCollector::NumaCollector(FileInterface files, std::vector<std::string> nodes)
    : _files(std::move(files)), _nodes(std::move(nodes)) {}

NumaCollector::~NumaCollector() { StopIntervalSampling(); }

auto NumaCollector::GetCapabilities() const -> CollectorCapability { return _collector_capability; }

auto NumaCollector::SetRawSampleSink(IRawSampleSink* raw_sample_sink) -> void {
  std::scoped_lock lock{_collection_mutex};
  _sample_sink = raw_sample_sink;
}

auto NumaCollector::ConfigureCollection(CollectionConfiguration&& configuration) -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state != CollectionState::UNCONFIGURED && _collection_state != CollectionState::CONFIGURED &&
      _collection_state != CollectionState::STOPPED) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  for (const auto* phase :
       {&configuration.Operations().operationsBeforeStart, &configuration.Operations().operationsAtStart,
        &configuration.Operations().operationsOnSample, &configuration.Operations().operationsAtStop}) {
    for (const auto& operation : *phase) {
      const auto* read = dynamic_cast<const NumaReadOperation*>(operation.get());
      if (read == nullptr ||
          (!read->node.empty() && std::find(_nodes.begin(), _nodes.end(), read->node) == _nodes.end())) {
        return ASTL_STATUS_BAD_ARGUMENT;
      }
    }
  }
  _previous.clear();
  _configuration    = std::move(configuration);
  _collection_state = CollectionState::CONFIGURED;
  return ExecuteCollectionOperations(_configuration->Operations().operationsBeforeStart);
}

auto NumaCollector::ClearCollectionState() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (_collection_state == CollectionState::STARTED || _collection_state == CollectionState::PAUSED) {
    return ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
  }
  _previous.clear();
  _configuration.reset();
  _collection_state = CollectionState::UNCONFIGURED;
  return ASTL_STATUS_SUCCESS;
}

auto NumaCollector::StartCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (const auto status = CheckCollectionLifecycleAction(_collection_state, CollectionLifecycleAction::START);
      status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (!_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  _previous.clear();

  auto result = ExecuteStartModeOperations();
  if (result == ASTL_STATUS_SUCCESS) {
    _collection_state = CollectionState::STARTED;
  }
  return result;
}

auto NumaCollector::PauseCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (const auto status = CheckCollectionLifecycleAction(_collection_state, CollectionLifecycleAction::PAUSE);
      status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  if (_periodic_sampler) {
    _periodic_sampler->Pause();
  } else {
    ASTL_LOG_WARNING("NumaCollector: PauseCollection called without an active periodic sampler");
  }
  _collection_state = CollectionState::PAUSED;
  return ASTL_STATUS_SUCCESS;
}

auto NumaCollector::ResumeCollection() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if (const auto status = CheckCollectionLifecycleAction(_collection_state, CollectionLifecycleAction::RESUME);
      status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  _previous.clear();
  if (_periodic_sampler) {
    _periodic_sampler->Resume();
  } else {
    ASTL_LOG_WARNING("NumaCollector: ResumeCollection called without an active periodic sampler");
  }
  _collection_state = CollectionState::STARTED;
  return ASTL_STATUS_SUCCESS;
}

auto NumaCollector::StopCollection() -> astl_status_code {
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
    _previous.clear();
  }
  return result;
}

auto NumaCollector::ReadImmediate() -> astl_status_code {
  std::scoped_lock lock{_collection_mutex};
  if ((_collection_state != CollectionState::STARTED && _collection_state != CollectionState::CONFIGURED &&
       _collection_state != CollectionState::PAUSED) ||
      !_configuration.has_value()) {
    return ASTL_STATUS_BAD_CONFIGURATION;
  }
  return ExecuteCollectionOperations(_configuration->Operations().operationsOnSample);
}

auto NumaCollector::ExecuteStartModeOperations() -> astl_status_code {
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

auto NumaCollector::ExecuteStopModeOperations() -> astl_status_code {
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

auto NumaCollector::StartIntervalSampling() -> astl_status_code {
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

auto NumaCollector::StopIntervalSampling() -> void { _periodic_sampler = nullptr; }

}  // namespace astl
