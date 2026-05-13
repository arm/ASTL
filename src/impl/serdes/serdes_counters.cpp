// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "astl_logger.hpp"
#include "common/capabilities.hpp"
#include "metric/counter.hpp"
#include "metric/i_counter.hpp"
#include "metric/metric_manager.hpp"
#include "serdes/serdes_metrics_detail.hpp"
#include "target.hpp"

namespace astl::ProtobufSerDes::detail {

RebuiltCounterHandles::RebuiltCounterHandles(
    std::vector<std::unique_ptr<CounterHandle>>                            counter_handles_in,
    std::unordered_map<const ITarget*, std::vector<astl_counter_handle_t>> target_to_counters_map_in)
    : counter_handles(std::move(counter_handles_in)), target_to_counters_map(std::move(target_to_counters_map_in)) {}

RebuiltCounterHandles::~RebuiltCounterHandles() = default;

RebuiltCounterHandles::RebuiltCounterHandles(RebuiltCounterHandles&&) noexcept = default;

auto RebuiltCounterHandles::operator=(RebuiltCounterHandles&&) noexcept -> RebuiltCounterHandles& = default;

static auto ValidateCounterHandleForSerialization(const CounterHandle& handle, const MetricConfig& cfg)
    -> std::expected<void, astl_status_code> {
  if (handle.target_to_counter_map.empty()) {
    ASTL_LOG_ERROR("SerializeCounterHandleToRawMetric: handle for counter {} has no targets", cfg.Name());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  for (const auto& [target, counter] : handle.target_to_counter_map) {
    if (!target) {
      ASTL_LOG_ERROR("SerializeCounterHandleToRawMetric: null target for counter {}", cfg.Name());
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    if (!counter) {
      ASTL_LOG_ERROR("SerializeCounterHandleToRawMetric: null counter instance for counter {}", cfg.Name());
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
  }

  return {};
}

static auto SerializeCounterHandleToRawMetric(const CounterHandle& handle)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  if (!handle.config) {
    ASTL_LOG_ERROR("SerializeCounterHandleToRawMetric: CounterHandle has null MetricConfig");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  const MetricConfig& cfg               = *handle.config;
  auto                validation_status = ValidateCounterHandleForSerialization(handle, cfg);
  if (!validation_status) {
    return std::unexpected(validation_status.error());
  }

  astl::protobuf::RawMetric raw;
  raw.set_metric_id(cfg.Id());

  auto cfg_or_err = SerializeBasicMetricConfig(cfg);
  if (!cfg_or_err) {
    ASTL_LOG_ERROR("SerializeCounterHandleToRawMetric: failed to serialize config for counter {}", cfg.Name());
    return std::unexpected(cfg_or_err.error());
  }
  *raw.mutable_config() = std::move(cfg_or_err.value());

  for (const auto& [target, counter] : handle.target_to_counter_map) {
    raw.add_target_ids(target->Name());
  }

  return raw;
}

auto SerializeCounterHandles(const std::vector<std::unique_ptr<CounterHandle>>& counter_handles,
                             astl::protobuf::MetricManager& proto_mgr) -> std::expected<void, astl_status_code> {
  auto* counters = proto_mgr.mutable_counters();

  for (const auto& handle_ptr : counter_handles) {
    if (!handle_ptr) {
      ASTL_LOG_ERROR("Serialize(MetricManager): null CounterHandle pointer in _counter_handles");
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }

    auto raw_or_err = SerializeCounterHandleToRawMetric(*handle_ptr);
    if (!raw_or_err) {
      return std::unexpected(raw_or_err.error());
    }

    *counters->add_metrics() = std::move(raw_or_err.value());
  }

  return {};
}

static auto ResolveRawMetricTargets(const astl::protobuf::RawMetric&             raw,
                                    const std::vector<std::unique_ptr<ITarget>>& targets, std::string_view object_kind)
    -> std::expected<std::vector<const ITarget*>, astl_status_code> {
  const auto& target_ids = raw.target_ids();
  if (target_ids.empty()) {
    ASTL_LOG_ERROR("ResolveRawMetricTargets: {} '{}' has zero target_ids", object_kind, raw.metric_id());
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  std::vector<const ITarget*> resolved_targets;
  resolved_targets.reserve(static_cast<std::vector<const ITarget*>::size_type>(target_ids.size()));
  std::unordered_set<std::string> seen_target_ids;

  for (const auto& target_id : target_ids) {
    if (!seen_target_ids.insert(target_id).second) {
      ASTL_LOG_ERROR("ResolveRawMetricTargets: duplicate target id '{}' for {} '{}'", target_id, object_kind,
                     raw.metric_id());
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    auto it = std::find_if(targets.begin(), targets.end(), [&target_id](const std::unique_ptr<ITarget>& target) {
      return target && target->Name() == target_id;
    });
    if (it == targets.end()) {
      ASTL_LOG_ERROR("ResolveRawMetricTargets: No target found with id '{}' for {} '{}'", target_id, object_kind,
                     raw.metric_id());
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    resolved_targets.push_back(it->get());
  }

  return resolved_targets;
}

static auto IsCollectorTypeSupported(const Capabilities& capabilities, CollectorType required_collector_type) -> bool {
  if (required_collector_type == CollectorType::ASTL_NATIVE) {
    return true;
  }

  const std::vector<CollectorCapability>& collector_caps = capabilities.GetCollectorCapability();
  return std::any_of(collector_caps.begin(), collector_caps.end(),
                     [required_collector_type](const CollectorCapability& cap) {
                       return cap.GetCollectorType() == required_collector_type;
                     });
}

auto RebuildCounterHandles(const astl::protobuf::MetricManager&         proto_manager,
                           const std::vector<std::unique_ptr<ITarget>>& targets, const Capabilities& capabilities)
    -> std::expected<RebuiltCounterHandles, astl_status_code> {
  RebuiltCounterHandles           rebuilt_counter_handles{{}, {}};
  std::unordered_set<std::string> seen_counter_ids;
  const auto&                     proto_counters_vec = proto_manager.counters();
  rebuilt_counter_handles.counter_handles.reserve(static_cast<std::size_t>(proto_counters_vec.metrics_size()));

  for (int i = 0; i < proto_counters_vec.metrics_size(); ++i) {
    const auto& raw = proto_counters_vec.metrics(i);
    if (!seen_counter_ids.insert(raw.metric_id()).second) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: duplicate counter metric_id '{}'", raw.metric_id());
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    auto cfg_or_err = DeserializeBasicMetricConfig(raw.config(), raw.metric_id());
    if (!cfg_or_err) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: failed to deserialize counter config '{}'", raw.metric_id());
      return std::unexpected(cfg_or_err.error());
    }

    if (!IsCollectorTypeSupported(capabilities, cfg_or_err.value()->GetCollectorType())) {
      return std::unexpected(ASTL_STATUS_UNSUPPORTED_COLLECTOR_TYPE);
    }

    auto targets_or_err = ResolveRawMetricTargets(raw, targets, "counter");
    if (!targets_or_err) {
      return std::unexpected(targets_or_err.error());
    }

    std::unordered_map<const ITarget*, std::unique_ptr<ICounter>> target_specific_counters;
    for (const auto* target : targets_or_err.value()) {
      target_specific_counters[target] = std::make_unique<Counter>(cfg_or_err.value().get(), target);
    }

    auto handle_unique_ptr =
        std::make_unique<CounterHandle>(std::move(cfg_or_err.value()), std::move(target_specific_counters));
    CounterHandle* raw_handle = handle_unique_ptr.get();

    for (const auto* target : targets_or_err.value()) {
      rebuilt_counter_handles.target_to_counters_map[target].push_back(static_cast<astl_counter_handle_t>(raw_handle));
    }

    rebuilt_counter_handles.counter_handles.emplace_back(std::move(handle_unique_ptr));
  }

  return rebuilt_counter_handles;
}

}  // namespace astl::ProtobufSerDes::detail
