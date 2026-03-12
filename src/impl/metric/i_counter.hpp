// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef I_COUNTER_HPP_
#define I_COUNTER_HPP_

#include <memory>
#include <unordered_map>

#include "astl/astl.h"
#include "common/metric_config.hpp"
#include "metric/raw_metric.hpp"
#include "target.hpp"

namespace astl {

/**
 * @brief Base class for counter types that return astl_counter_props_t API information
 *        All implementors of ICounter are also RawMetric implementors so that MetricManager
 *        can use both interfaces on its counters and still have test mocks for ICounter implementations
 */
struct ICounter : public virtual IMetric {
  ICounter() = default;

  ~ICounter() override = default;

  ICounter(const ICounter&)            = default;
  ICounter& operator=(const ICounter&) = default;
  ICounter(ICounter&&)                 = default;
  ICounter& operator=(ICounter&&)      = default;

  // don't hide the base class GetProperties(astl_metric_props_t overload)
  using IMetric::GetProperties;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  virtual auto GetProperties(astl_counter_props_t* properties) const -> astl_status_code = 0;
};

/**
 * @struct CounterHandle - internal representation of a astl_counter_handle_t
 * @brief Holds details of a single counter including its configuration and associated targets.
 */
struct CounterHandle {
  // configuration (properties + operation builder) specific to a counter common across many targets
  std::unique_ptr<MetricConfig>                                 config;
  std::unordered_map<const ITarget*, std::unique_ptr<ICounter>> target_to_counter_map;

  CounterHandle() = default;
  CounterHandle(std::unique_ptr<MetricConfig>                                 counter_config,
                std::unordered_map<const ITarget*, std::unique_ptr<ICounter>> target_counter_map)
      : config(std::move(counter_config)), target_to_counter_map(std::move(target_counter_map)) {}
};

}  // namespace astl

#endif  // I_COUNTER_HPP_
