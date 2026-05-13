// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SERDES_METRICS_DETAIL_HPP_
#define SERDES_METRICS_DETAIL_HPP_

#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "astl/astl_errors.h"
#include "serdes/metrics.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.

namespace astl {

class MetricConfig;
struct Capabilities;
struct CounterHandle;
struct ITarget;

namespace ProtobufSerDes::detail {

struct RebuiltCounterHandles {
  RebuiltCounterHandles(
      std::vector<std::unique_ptr<CounterHandle>>                            counter_handles_in,
      std::unordered_map<const ITarget*, std::vector<astl_counter_handle_t>> target_to_counters_map_in);
  ~RebuiltCounterHandles();

  RebuiltCounterHandles(const RebuiltCounterHandles&)            = delete;
  RebuiltCounterHandles& operator=(const RebuiltCounterHandles&) = delete;
  RebuiltCounterHandles(RebuiltCounterHandles&&) noexcept;
  RebuiltCounterHandles& operator=(RebuiltCounterHandles&&) noexcept;

  std::vector<std::unique_ptr<CounterHandle>>                            counter_handles;
  std::unordered_map<const ITarget*, std::vector<astl_counter_handle_t>> target_to_counters_map;
};

auto SerializeBasicMetricConfig(const MetricConfig& config)
    -> std::expected<astl::protobuf::MetricConfig, astl_status_code>;

auto DeserializeBasicMetricConfig(const astl::protobuf::MetricConfig& proto_cfg, const std::string& metric_id)
    -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code>;

auto SerializeCounterHandles(const std::vector<std::unique_ptr<CounterHandle>>& counter_handles,
                             astl::protobuf::MetricManager& proto_mgr) -> std::expected<void, astl_status_code>;

auto RebuildCounterHandles(const astl::protobuf::MetricManager&         proto_manager,
                           const std::vector<std::unique_ptr<ITarget>>& targets, const Capabilities& capabilities)
    -> std::expected<RebuiltCounterHandles, astl_status_code>;

}  // namespace ProtobufSerDes::detail

}  // namespace astl

#endif  // SERDES_METRICS_DETAIL_HPP_
