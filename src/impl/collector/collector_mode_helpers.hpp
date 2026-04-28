// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef COLLECTOR_MODE_HELPERS_HPP_
#define COLLECTOR_MODE_HELPERS_HPP_

#include <utility>

#include "collector/collection_configuration.hpp"

namespace astl::collector_detail {

template <typename ExecuteOperationsFn, typename StartSamplingFn>
auto ExecuteStartMode(const CollectionConfiguration& configuration, ExecuteOperationsFn&& execute_operations,
                      StartSamplingFn&& start_sampling) -> astl_status_code {
  auto status = std::forward<ExecuteOperationsFn>(execute_operations)(configuration.Operations().operationsAtStart);
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  switch (configuration.CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      return ASTL_STATUS_SUCCESS;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      return std::forward<ExecuteOperationsFn>(execute_operations)(configuration.Operations().operationsOnSample);
    case ASTL_COLLECTION_MODE_SAMPLING:
      return std::forward<StartSamplingFn>(start_sampling)();
    default:
      return ASTL_STATUS_BAD_CONFIGURATION;
  }
}

template <typename ExecuteOperationsFn>
auto ExecuteStopMode(const CollectionConfiguration& configuration, ExecuteOperationsFn&& execute_operations)
    -> astl_status_code {
  astl_status_code status = ASTL_STATUS_SUCCESS;
  switch (configuration.CollectionParams().collection_mode) {
    case ASTL_COLLECTION_MODE_IMMEDIATE:
      break;
    case ASTL_COLLECTION_MODE_SNAPSHOT:
      status = std::forward<ExecuteOperationsFn>(execute_operations)(configuration.Operations().operationsOnSample);
      if (status != ASTL_STATUS_SUCCESS) {
        return status;
      }
      break;
    case ASTL_COLLECTION_MODE_SAMPLING:
      break;
    default:
      return ASTL_STATUS_BAD_CONFIGURATION;
  }

  return std::forward<ExecuteOperationsFn>(execute_operations)(configuration.Operations().operationsAtStop);
}

}  // namespace astl::collector_detail

#endif  // COLLECTOR_MODE_HELPERS_HPP_
