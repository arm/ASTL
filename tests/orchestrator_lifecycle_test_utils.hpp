// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mock_classes.hpp"
#include "orchestrator/orchestrator.hpp"
#include "test_includes.hpp"

// Work around unconstrained std::expected/nullptr comparisons in the mock library.
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

// Establish a real running state for tests focused on sample processing or output.
inline void StartTestCollection(astl::Orchestrator& orchestrator, MockCollectorManager& collectors,
                                MockMetricManager& metrics, const astl::ITarget* target) {
  using trompeloeil::_;
  ALLOW_CALL(collectors, GetNativeClockSnapshot(_))
      .RETURN(std::expected<astl::ClockCorrelationMap, astl_status_code>{astl::ClockCorrelationMap{}});
  ALLOW_CALL(collectors, StartOnTarget(_)).RETURN(ASTL_STATUS_SUCCESS);
  ALLOW_CALL(metrics, SetClockCorrelations(_));
  REQUIRE(orchestrator.StartCollection(target) == ASTL_STATUS_SUCCESS);
}

inline void ConfigureAndStartTestCollection(astl::Orchestrator& orchestrator, MockCollectorManager& collectors,
                                            MockMetricManager& metrics, const astl::ITarget* target) {
  using trompeloeil::_;
  ALLOW_CALL(metrics, GetAvailableCounters(_))
      .RETURN(std::expected<std::span<const astl_counter_handle_t>, astl_status_code>{
          std::span<const astl_counter_handle_t>{}});
  ALLOW_CALL(metrics, GetCounterRequiredOperations(_, _))
      .RETURN(std::expected<astl::CollectionOperations, astl_status_code>{
          astl::CollectionOperations{
                                     {}, {}, {}, {}, astl::SamplingInterval{0}, astl::CollectorCapability{astl::CollectorType::UNKNOWN}}
  });
  ALLOW_CALL(collectors, ConfigureCollectionOnTarget(_, _, _)).RETURN(ASTL_STATUS_SUCCESS);
  astl_collection_params_t params{};
  params.size            = sizeof(params);
  params.collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE;
  REQUIRE(orchestrator.ConfigureCounterCollection(target, &params, {}) == ASTL_STATUS_SUCCESS);
  StartTestCollection(orchestrator, collectors, metrics, target);
}
