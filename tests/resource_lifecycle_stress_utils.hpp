// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_RESOURCE_LIFECYCLE_STRESS_UTILS_HPP_
#define ASTL_RESOURCE_LIFECYCLE_STRESS_UTILS_HPP_

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <vector>

#include "astl/astl_errors.h"
#include "collector/collection_configuration.hpp"
#include "common/clock_correlation.hpp"
#include "mock_classes.hpp"
#include "operation/operation.hpp"

namespace astl {
inline auto operator==(const CollectionOperations& lhs, std::nullptr_t rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}
inline auto operator==(std::nullptr_t lhs, const CollectionOperations& rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}
}  // namespace astl

namespace std {
template <typename T, std::size_t Extent>
inline auto operator==(span<T, Extent> lhs, std::nullptr_t rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}

template <typename T, std::size_t Extent>
inline auto operator==(std::nullptr_t lhs, span<T, Extent> rhs) -> bool {
  (void)lhs;
  (void)rhs;
  return false;
}
}  // namespace std

namespace astl::testing {
constexpr int kResourceLifecycleStressIterations = 64;

inline auto MakeSingleSampleOperation() -> std::expected<CollectionOperations, astl_status_code> {
  CollectionOperations operations{{}, {}, {}, {}, SamplingInterval{0}, CollectorCapability{CollectorType::UNKNOWN}};
  operations.operationsOnSample.push_back(std::make_unique<Operation>());
  return operations;
}

inline auto MakeTestClockCorrelations() -> std::expected<ClockCorrelationMap, astl_status_code> {
  return ClockCorrelationMap{};
}

using StressExpectations = std::vector<std::unique_ptr<trompeloeil::expectation>>;

inline auto MakeMetricLifecycleCollectionExpectations(MockMetricManager&                    metric_manager,
                                                      std::span<const astl_metric_handle_t> available_metrics)
    -> StressExpectations {
  using trompeloeil::_;
  StressExpectations expectations;
  expectations.push_back(NAMED_ALLOW_CALL(metric_manager, RegisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(
      NAMED_ALLOW_CALL(metric_manager, UnregisterProcessedSampleSink(_)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(
      NAMED_ALLOW_CALL(metric_manager, GetAvailableMetrics(_))
          .RETURN(std::expected<std::span<const astl_metric_handle_t>, astl_status_code>{available_metrics}));
  expectations.push_back(
      NAMED_ALLOW_CALL(metric_manager, GetRequiredOperations(_, _)).LR_RETURN(MakeSingleSampleOperation()));
  expectations.push_back(NAMED_ALLOW_CALL(metric_manager, SetClockCorrelations(_)));
  expectations.push_back(NAMED_ALLOW_CALL(metric_manager, GetLifecycleEventMetricOnTarget(_)).RETURN(nullptr));
  expectations.push_back(NAMED_ALLOW_CALL(metric_manager, RegisterMetric(_, _)).RETURN(ASTL_STATUS_SUCCESS));
  expectations.push_back(NAMED_ALLOW_CALL(metric_manager, SummarizeMetrics()).RETURN(ASTL_STATUS_SUCCESS));
  return expectations;
}

inline auto KeepExpectationsAlive(const StressExpectations& expectations) -> void { (void)expectations; }
}  // namespace astl::testing

#endif  // ASTL_RESOURCE_LIFECYCLE_STRESS_UTILS_HPP_
