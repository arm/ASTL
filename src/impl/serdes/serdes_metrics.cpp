/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "metric/metric_manager.hpp"
#include "metric/sampled_value_metric.hpp"
#include "orchestrator/orchestrator.hpp"
#include "serdes/metrics.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.
#include "serdes/protobuf_serdes.hpp"

namespace astl::ProtobufSerDes {

namespace detail {

static auto SerializeBasicMetricConfig(const MetricConfig& config)
    -> std::expected<astl::protobuf::MetricConfig, astl_status_code> {
  astl::protobuf::MetricConfig out;

  out.set_metric_name(config.Name());
  out.set_description(config.Description());
  out.set_units(static_cast<astl::protobuf::AstlUnits>(config.Units()));
  out.set_value_type(static_cast<astl::protobuf::AstlValueType>(config.ValueType()));
  out.set_metric_type(static_cast<astl::protobuf::AstlMetricType>(config.MetricType()));
  out.set_category(static_cast<astl::protobuf::AstlCategory>(config.Category()));

  for (const auto& group : config.MetricGroups()) {
    out.add_metric_groups(group);
  }

  out.set_collector_type(static_cast<astl::protobuf::CollectorType>(config.GetCollectorType()));

  return out;
}

static auto SerializeBasicMetric(const MetricConfig& metric_config, const ITarget& target)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  astl::protobuf::RawMetric out;

  out.set_metric_id(metric_config.Name());
  out.add_target_ids(target.Name());

  auto cfg_or_err = SerializeBasicMetricConfig(metric_config);
  if (!cfg_or_err) {
    ASTL_LOG_ERROR("SerializeSampledValueMetric: failed to serialize MetricConfig for metric {}", metric_config.Name());
    return std::unexpected(cfg_or_err.error());
  }
  *out.mutable_config() = *cfg_or_err;

  return out;
}

static auto SerializeIMetric(const MetricConfig& metric_config, const ITarget& target)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  switch (metric_config.MetricType()) {
    case ASTL_METRIC_VALUE:
    case ASTL_METRIC_EVENT:
    case ASTL_METRIC_DELTA:
    case ASTL_METRIC_RATE: {
      return SerializeBasicMetric(metric_config, target);
    }
    default:
      ASTL_LOG_ERROR("Serialize not implemented for metric type {}", static_cast<int>(metric_config.MetricType()));
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
}

static auto DeserializeBasicMetricConfig(const astl::protobuf::MetricConfig& proto_cfg)
    -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code> {
  const std::string& name        = proto_cfg.metric_name();
  const std::string& description = proto_cfg.description();

  const auto units       = static_cast<astl_units_t>(proto_cfg.units());
  const auto value_type  = static_cast<astl_value_type_t>(proto_cfg.value_type());
  const auto category    = static_cast<astl_category_t>(proto_cfg.category());
  const auto metric_type = static_cast<astl_metric_type_t>(proto_cfg.metric_type());
  const auto collector   = static_cast<CollectorType>(proto_cfg.collector_type());

  if (proto_cfg.metric_groups_size() == 0) {
    auto cfg = std::make_unique<MetricConfig>(name, description, units, value_type, category, metric_type, collector,
                                              NullOperationBuilder{});
    return cfg;
  }

  std::vector<std::string> groups{proto_cfg.metric_groups().begin(), proto_cfg.metric_groups().end()};
  auto cfg = std::make_unique<MetricConfig>(name, description, units, value_type, category, metric_type, groups,
                                            collector, NullOperationBuilder{});

  return cfg;
}

template <typename MetricT>
static auto DeserializeBasicMetric(const astl::protobuf::RawMetric& raw, const MetricConfig& metric_config)
    -> std::expected<std::vector<std::pair<std::unique_ptr<IMetric>, const ITarget*>>, astl_status_code> {
  const auto& orch       = Orchestrator::GetInstance()->get();
  const auto& targets    = orch->GetTargets();
  const auto& target_ids = raw.target_ids();

  if (target_ids.empty()) {
    ASTL_LOG_ERROR("DeserializeBasicMetric: RawMetric has zero target_ids for metric {}", metric_config.Name());
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  auto* sink = dynamic_cast<IProcessedSampleSink*>(orch->GetMetricManager().get());
  if (!sink) {
    ASTL_LOG_ERROR("DeserializeBasicMetric: GetMetricManager() is not an IProcessedSampleSink");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  std::vector<std::pair<std::unique_ptr<IMetric>, const ITarget*>> result;
  result.reserve(static_cast<decltype(result)::size_type>(target_ids.size()));

  for (const auto& target_id : target_ids) {
    auto target_it = std::find_if(targets.begin(), targets.end(), [&target_id](auto const& owned_target) {
      return owned_target && owned_target->Name() == target_id;
    });

    if (target_it == targets.end()) {
      ASTL_LOG_ERROR("DeserializeBasicMetric: No target found with id '{}' for metric {}", target_id,
                     metric_config.Name());
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    const ITarget* target = target_it->get();
    auto           metric = std::make_unique<MetricT>(std::addressof(metric_config), target, sink);

    result.emplace_back(std::move(metric), target);
  }

  return result;
}

static auto DeserializeMetricForType(astl_metric_type_t metric_type, const astl::protobuf::RawMetric& raw,
                                     const MetricConfig& config)
    -> std::expected<std::vector<std::pair<std::unique_ptr<IMetric>, const ITarget*>>, astl_status_code> {
  switch (metric_type) {
    case ASTL_METRIC_VALUE:
    case ASTL_METRIC_EVENT:
    case ASTL_METRIC_DELTA:
    case ASTL_METRIC_RATE: {
      auto metrics_or_err = DeserializeBasicMetric<SampledValueMetric>(raw, config);

      if (!metrics_or_err) {
        ASTL_LOG_ERROR(
            "DeserializeMetricForType: DeserializeBasicMetric failed "
            "for metric {}",
            config.Name());
        return std::unexpected(metrics_or_err.error());
      }

      // metric_or_err holds vector<pair<unique_ptr<IMetric>, const ITarget*>>
      return std::move(*metrics_or_err);
    }

    // TODO(ASTL-238) Implement deserialization for other metric types
    case ASTL_METRIC_RESIDENCY:
    case ASTL_METRIC_FINITE_SET_VALUE:
    case ASTL_METRIC_UNKNOWN:
    default:
      ASTL_LOG_ERROR(
          "DeserializeMetricForType: Deserialization not implemented for "
          "metric type {} (metric: {})",
          static_cast<int>(metric_type), config.Name());
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
}

static auto DeserializeMetricHandle(const astl::protobuf::RawMetric& raw)
    -> std::expected<MetricHandle, astl_status_code> {
  MetricHandle metric_handle{};

  auto cfg_or_err = DeserializeBasicMetricConfig(raw.config());
  if (!cfg_or_err) {
    ASTL_LOG_ERROR("DeserializeMetricHandle: failed to deserialize MetricConfig for metric {}", raw.metric_id());
    return std::unexpected(cfg_or_err.error());
  }

  metric_handle.config            = std::move(*cfg_or_err);
  const MetricConfig& config      = *metric_handle.config;
  const auto          metric_type = config.MetricType();

  auto metrics_or_err = DeserializeMetricForType(metric_type, raw, config);
  if (!metrics_or_err) {
    ASTL_LOG_ERROR(
        "DeserializeMetricHandle: DeserializeMetricForType failed "
        "for metric {}",
        config.Name());
    return std::unexpected(metrics_or_err.error());
  }

  // metrics_or_err holds vector<pair<unique_ptr<IMetric>, const ITarget*>>
  for (auto& metric_and_target : *metrics_or_err) {
    auto&          metric = metric_and_target.first;
    const ITarget* target = metric_and_target.second;

    if (!target) {
      ASTL_LOG_ERROR("DeserializeMetricHandle: Deserialized metric has null target for metric {}", config.Name());
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }

    auto [it, inserted] = metric_handle.target_to_metric_map.emplace(target, std::move(metric));
    if (!inserted) {
      ASTL_LOG_ERROR("DeserializeMetricHandle: duplicate target '{}' in RawMetric for metric {}", target->Name(),
                     config.Name());
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }
  }

  return metric_handle;
}

}  // namespace detail

auto Serialize(const MetricHandle& handle, std::ostream& output_stream) -> astl_status_code {
  if (!handle.config) {
    ASTL_LOG_ERROR("Serialize(MetricHandle): handle has null MetricConfig");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  if (handle.target_to_metric_map.empty()) {
    ASTL_LOG_WARNING("Serialize(MetricHandle): handle for metric {} has no metrics", handle.config->Name());
    return ASTL_STATUS_SUCCESS;
  }

  const MetricConfig&          config = *handle.config;
  astl::protobuf::RawMetricVec vec{};

  const auto&    first_entry  = *handle.target_to_metric_map.begin();
  const ITarget& first_target = *first_entry.first;

  auto proto_or_err = detail::SerializeIMetric(config, first_target);
  if (!proto_or_err) {
    ASTL_LOG_ERROR(
        "Serialize(MetricHandle): SerializeIMetric failed for metric '{}' "
        "(target '{}') with status {}",
        handle.config->Name(), first_target.Name(), astlStatusString(proto_or_err.error()));
    return proto_or_err.error();
  }

  astl::protobuf::RawMetric raw = std::move(proto_or_err.value());
  raw.clear_target_ids();

  for (const auto& entry : handle.target_to_metric_map) {
    const ITarget* target = entry.first;
    if (!target) {
      ASTL_LOG_ERROR("Serialize(MetricHandle): null target in target_to_metric_map for metric {}",
                     handle.config->Name());
      return ASTL_STATUS_INTERNAL_ERROR;
    }
    raw.add_target_ids(target->Name());
  }

  *vec.add_metrics() = std::move(raw);

  if (!vec.SerializeToOstream(&output_stream)) {
    ASTL_LOG_ERROR("Serialize(MetricHandle): Failed to serialize RawMetricVec for metric '{}'", handle.config->Name());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

template <>
auto Deserialize<std::vector<std::unique_ptr<MetricHandle>>>(std::istream& input)
    -> std::expected<std::vector<std::unique_ptr<MetricHandle>>, astl_status_code> {
  astl::protobuf::RawMetricVec proto_metrics;
  if (!proto_metrics.ParseFromIstream(&input)) {
    ASTL_LOG_ERROR("Deserialize<vector<MetricHandle>>: Failed to parse RawMetricVec");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  std::vector<std::unique_ptr<MetricHandle>> handles;
  handles.reserve(static_cast<decltype(handles)::size_type>(proto_metrics.metrics_size()));

  for (const auto& raw : proto_metrics.metrics()) {
    auto handle_or_err = detail::DeserializeMetricHandle(raw);
    if (!handle_or_err) {
      ASTL_LOG_ERROR("Deserialize<vector<MetricHandle>>: Failed to deserialize metric '{}'", raw.metric_id());
      return std::unexpected(handle_or_err.error());
    }

    handles.emplace_back(std::make_unique<MetricHandle>(std::move(*handle_or_err)));
  }

  return handles;
}

}  // namespace astl::ProtobufSerDes
