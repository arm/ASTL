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
#include "astl_logger.hpp"
#include "common/string_pool.hpp"
#include "metric/delta_metric.hpp"
#include "metric/event_metric.hpp"
#include "metric/finite_set_metric.hpp"
#include "metric/metric_manager.hpp"
#include "metric/rate_metric.hpp"
#include "metric/sampled_value_metric.hpp"
#include "serdes/metrics.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.
#include "serdes/protobuf_serdes.hpp"

namespace astl::ProtobufSerDes {

namespace detail {

/**
 * @brief Serializes a MetricConfig into a protobuf representation.
 *
 * Converts a MetricConfig object into its corresponding protobuf message format,
 * including metric metadata (name, units, type, category) and associated metric groups.
 *
 * @param config The MetricConfig to serialize.
 *
 * @return An expected containing the serialized protobuf::MetricConfig on success,
 *         or an astl_status_code error code on failure.
 *
 * @note The function interns certain string values (e.g., finite-set labels) into
 *       the global string pool via GetInternedString() calls. These intern operations
 *       are performed to pre-populate the string pool for potential reuse during
 *       deserialization or other downstream operations, even though the interned
 *       string handles are discarded. If this behavior is not needed, consider
 *       removing these calls to reduce overhead.
 */
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

static auto SerializeBasicMetric(const MetricConfig& metric_config, const ITarget* target)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  astl::protobuf::RawMetric out;
  if (!target) {
    ASTL_LOG_ERROR("SerializeBasicMetric: target is null for metric {}", metric_config.Name());
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  out.set_metric_id(metric_config.Name());
  out.add_target_ids(target->Name());

  auto cfg_or_err = SerializeBasicMetricConfig(metric_config);
  if (!cfg_or_err) {
    ASTL_LOG_ERROR("SerializeSampledValueMetric: failed to serialize MetricConfig for metric {}", metric_config.Name());
    return std::unexpected(cfg_or_err.error());
  }
  *out.mutable_config() = *cfg_or_err;

  return out;
}

static auto SerializeFiniteSetMetric(const MetricConfig& metric_config, const ITarget* target)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  auto out_or_err = SerializeBasicMetric(metric_config, target);
  if (!out_or_err) {
    ASTL_LOG_ERROR("SerializeFiniteSetMetric: failed to serialize basic metric for metric {}", metric_config.Name());
    return out_or_err;
  }

  auto& raw_metric = out_or_err.value();
  auto* config     = raw_metric.mutable_config();

  config->set_metric_type(protobuf::AstlMetricType::ASTL_METRIC_FINITE_SET_VALUE);

  const auto& finite_set_config = dynamic_cast<const FiniteSetMetricConfig&>(metric_config);

  auto* finite_set_msg = config->mutable_finite_set();

  for (const auto& [value, label] : finite_set_config.GetLabels()) {
    (*finite_set_msg->mutable_value_to_label_map())[*value.ToInt64()] = label;
  }

  return out_or_err;
}

static auto SerializeIMetric(const MetricConfig& metric_config, const ITarget* target)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  switch (metric_config.MetricType()) {
    case ASTL_METRIC_VALUE:
    case ASTL_METRIC_EVENT:
    case ASTL_METRIC_DELTA:
    case ASTL_METRIC_RATE: {
      return SerializeBasicMetric(metric_config, target);
    }
    case ASTL_METRIC_FINITE_SET_VALUE: {
      return SerializeFiniteSetMetric(metric_config, target);
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

static auto DeserializeFiniteSetMetricConfig(const astl::protobuf::MetricConfig& proto_cfg)
    -> std::expected<std::unique_ptr<FiniteSetMetricConfig>, astl_status_code> {
  if (!proto_cfg.has_finite_set()) {
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  const auto&        finite_set_cfg_proto = proto_cfg.finite_set();
  const std::string& name                 = proto_cfg.metric_name();
  const std::string& description          = proto_cfg.description();

  const auto units       = static_cast<astl_units_t>(proto_cfg.units());
  const auto value_type  = static_cast<astl_value_type_t>(proto_cfg.value_type());
  const auto category    = static_cast<astl_category_t>(proto_cfg.category());
  const auto metric_type = static_cast<astl_metric_type_t>(proto_cfg.metric_type());
  const auto collector   = static_cast<CollectorType>(proto_cfg.collector_type());

  FiniteSetMetricConfig::FiniteSet       finite_set;
  FiniteSetMetricConfig::ValueToLabelMap labels;
  try {
    for (const auto& entry : finite_set_cfg_proto.value_to_label_map()) {
      const auto key = static_cast<uint64_t>(entry.first);
      finite_set.emplace(key);
      labels.emplace(AstlValue(key), entry.second);
    }
  } catch (const std::bad_variant_access& e) {
    ASTL_LOG_ERROR("DeserializeFiniteSetMetricConfig: bad variant access when parsing finite set for metric {}: {}",
                   name, e.what());
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  if (proto_cfg.metric_groups_size() > 0) {
    ASTL_LOG_WARNING("DeserializeFiniteSetMetricConfig: Metric groups not implemented for FiniteSetMetricConfig {}",
                     name);
  }

  AnyFormula formula = IdentityFormula{};
  auto       cfg = std::make_unique<FiniteSetMetricConfig>(name, description, units, value_type, metric_type, category,
                                                           collector, NullOperationBuilder{}, std::move(finite_set),
                                                           std::move(labels), std::move(formula));
  return cfg;
}

struct MetricDeserializationResult {
  const ITarget*           target{};
  std::unique_ptr<IMetric> metric;
};

using MetricDeserializationResults = std::vector<MetricDeserializationResult>;
using MetricConfigAndResults       = std::pair<std::unique_ptr<MetricConfig>, MetricDeserializationResults>;

template <typename MetricT, typename ConfigT>
static auto DeserializeBasicMetric(const astl::protobuf::RawMetric& raw, ConfigT* metric_config,
                                   const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<MetricDeserializationResults, astl_status_code> {
  const auto& target_ids = raw.target_ids();

  if (target_ids.empty()) {
    ASTL_LOG_ERROR("DeserializeBasicMetric: RawMetric has zero target_ids for metric {}", metric_config->Name());
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  MetricDeserializationResults result;
  result.reserve(static_cast<MetricDeserializationResults::size_type>(target_ids.size()));

  for (const auto& target_id : target_ids) {
    auto it = std::find_if(targets.begin(), targets.end(), [&target_id](const std::unique_ptr<ITarget>& owned_target) {
      return owned_target && owned_target->Name() == target_id;
    });

    if (it == targets.end()) {
      ASTL_LOG_ERROR("DeserializeBasicMetric: No target found with id '{}' for metric {}", target_id,
                     metric_config->Name());
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    const ITarget* target = it->get();
    auto           metric = std::make_unique<MetricT>(metric_config, target, nullptr);

    result.push_back(MetricDeserializationResult{
        target,
        std::move(metric),
    });
  }

  return result;
}

static auto DeserializeMetricForType(astl_metric_type_t metric_type, const astl::protobuf::RawMetric& raw,
                                     const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<MetricConfigAndResults, astl_status_code> {
  switch (metric_type) {
    case ASTL_METRIC_VALUE: {
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config());
      if (!cfg_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricHandle: failed to deserialize MetricConfig for metric {}", raw.metric_id());
        return std::unexpected(cfg_or_err.error());
      }

      auto& cfg            = cfg_or_err.value();
      auto  metrics_or_err = DeserializeBasicMetric<SampledValueMetric>(raw, cfg.get(), targets);
      if (!metrics_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricForType: DeserializeBasicMetric failed for metric {}", cfg->Name());
        return std::unexpected(metrics_or_err.error());
      }

      return MetricConfigAndResults{std::move(cfg), std::move(metrics_or_err.value())};
    }

    case ASTL_METRIC_EVENT: {
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config());
      if (!cfg_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricHandle: failed to deserialize MetricConfig for metric {}", raw.metric_id());
        return std::unexpected(cfg_or_err.error());
      }

      auto& cfg            = cfg_or_err.value();
      auto  metrics_or_err = DeserializeBasicMetric<EventMetric>(raw, cfg.get(), targets);
      if (!metrics_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricForType: DeserializeBasicMetric failed for metric {}", cfg->Name());
        return std::unexpected(metrics_or_err.error());
      }

      return MetricConfigAndResults{std::move(cfg), std::move(metrics_or_err.value())};
    }

    case ASTL_METRIC_DELTA: {
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config());
      if (!cfg_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricHandle: failed to deserialize MetricConfig for metric {}", raw.metric_id());
        return std::unexpected(cfg_or_err.error());
      }

      auto& cfg            = cfg_or_err.value();
      auto  metrics_or_err = DeserializeBasicMetric<DeltaMetric>(raw, cfg.get(), targets);
      if (!metrics_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricForType: DeserializeBasicMetric failed for metric {}", cfg->Name());
        return std::unexpected(metrics_or_err.error());
      }

      return MetricConfigAndResults{std::move(cfg), std::move(metrics_or_err.value())};
    }

    case ASTL_METRIC_RATE: {
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config());
      if (!cfg_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricHandle: failed to deserialize MetricConfig for metric {}", raw.metric_id());
        return std::unexpected(cfg_or_err.error());
      }

      auto& cfg            = cfg_or_err.value();
      auto  metrics_or_err = DeserializeBasicMetric<RateMetric>(raw, cfg.get(), targets);
      if (!metrics_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricForType: DeserializeBasicMetric failed for metric {}", cfg->Name());
        return std::unexpected(metrics_or_err.error());
      }

      return MetricConfigAndResults{std::move(cfg), std::move(metrics_or_err.value())};
    }

    case ASTL_METRIC_FINITE_SET_VALUE: {
      auto cfg_or_err = DeserializeFiniteSetMetricConfig(raw.config());
      if (!cfg_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricHandle: failed to deserialize MetricConfig for metric {}", raw.metric_id());
        return std::unexpected(cfg_or_err.error());
      }

      auto& finite_cfg = cfg_or_err.value();
      auto  metrics_or_err =
          DeserializeBasicMetric<FiniteSetMetric, FiniteSetMetricConfig>(raw, finite_cfg.get(), targets);
      if (!metrics_or_err) {
        ASTL_LOG_ERROR("DeserializeMetricForType: DeserializeBasicMetric failed for metric {}", finite_cfg->Name());
        return std::unexpected(metrics_or_err.error());
      }

      return MetricConfigAndResults{std::move(finite_cfg), std::move(metrics_or_err.value())};
    }

    case ASTL_METRIC_RESIDENCY:
    case ASTL_METRIC_UNKNOWN:
    default:
      ASTL_LOG_ERROR("DeserializeMetricForType: Deserialization not implemented for metric type {}",
                     static_cast<int>(metric_type));
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }
}

static auto SerializeHandleToRawMetric(const MetricHandle& handle)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  if (!handle.config) {
    ASTL_LOG_ERROR("SerializeHandleToRawMetric: MetricHandle has null MetricConfig");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  const MetricConfig& cfg = *handle.config;

  if (handle.target_to_metric_map.empty()) {
    ASTL_LOG_ERROR("SerializeHandleToRawMetric: handle for metric {} has no targets", cfg.Name());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  const auto&    first_entry = *handle.target_to_metric_map.begin();
  const ITarget* first_ptr   = first_entry.first;
  if (!first_ptr) {
    ASTL_LOG_ERROR("SerializeHandleToRawMetric: first target is null for metric {}", cfg.Name());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto proto_or_err = SerializeIMetric(cfg, first_ptr);
  if (!proto_or_err) {
    ASTL_LOG_ERROR(
        "SerializeHandleToRawMetric: SerializeIMetric failed for metric '{}' "
        "(target '{}') with status {}",
        cfg.Name(), first_ptr->Name(), astlStatusString(proto_or_err.error()));
    return std::unexpected(proto_or_err.error());
  }

  astl::protobuf::RawMetric raw = std::move(proto_or_err.value());
  raw.clear_target_ids();

  for (const auto& [target, metric] : handle.target_to_metric_map) {
    if (!target) {
      ASTL_LOG_ERROR("SerializeHandleToRawMetric: null target in target_to_metric_map for metric {}", cfg.Name());
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    raw.add_target_ids(target->Name());
  }

  return raw;
}

static auto SerializeHandleToRawMetricVec(const MetricHandle& handle)
    -> std::expected<astl::protobuf::RawMetricVec, astl_status_code> {
  if (!handle.config) {
    ASTL_LOG_ERROR("SerializeHandleToRawMetricVec: MetricHandle has null MetricConfig");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  astl::protobuf::RawMetricVec vec;

  if (handle.target_to_metric_map.empty()) {
    return vec;
  }

  auto raw_or_err = SerializeHandleToRawMetric(handle);
  if (!raw_or_err) {
    return std::unexpected(raw_or_err.error());
  }

  astl::protobuf::RawMetric raw = std::move(raw_or_err.value());
  *vec.add_metrics()            = std::move(raw);

  return vec;
}

auto SerializeCollectorCapabilities(const astl::Capabilities& caps, astl::protobuf::MetricManager& proto_mgr) -> void {
  for (const auto& cap : caps._collector_capabilities) {
    proto_mgr.add_capabilities(static_cast<astl::protobuf::CollectorType>(cap.collector_type));
  }
}

auto SerializeMetricHandles(const std::vector<std::unique_ptr<MetricHandle>>& metric_handles,
                            astl::protobuf::MetricManager& proto_mgr) -> std::expected<void, astl_status_code> {
  auto* metrics = proto_mgr.mutable_metrics();

  for (const auto& handle_ptr : metric_handles) {
    if (!handle_ptr) {
      ASTL_LOG_ERROR("Serialize(MetricManager): null MetricHandle pointer in _metric_handles");
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }

    auto vec_or_err = detail::SerializeHandleToRawMetricVec(*handle_ptr);
    if (!vec_or_err) {
      return std::unexpected(vec_or_err.error());
    }

    for (const auto& metric : vec_or_err->metrics()) {
      *metrics->add_metrics() = metric;  // copy; vec has 0 or 1 RawMetric
    }
  }

  return {};
}

auto SerializeMetricGroups(const std::vector<std::unique_ptr<MetricGroup>>& metric_groups,
                           astl::protobuf::MetricManager&                   proto_mgr) -> astl_status_code {
  for (const auto& group : metric_groups) {
    if (!group) {
      ASTL_LOG_ERROR("Serialize(MetricManager): null MetricGroup in _metric_groups");
      return ASTL_STATUS_INTERNAL_ERROR;
    }

    auto* proto_group = proto_mgr.add_metric_groups();
    proto_group->set_group_name(group->name);
    proto_group->set_group_description(group->description);
  }

  return ASTL_STATUS_SUCCESS;
}

auto SerializeOperationToMetricMap(const MetricManager::OperationToMetricMap& op_map,
                                   const MetricManager& metric_manager, astl::protobuf::MetricManager& proto_mgr)
    -> astl_status_code {
  auto* proto_op_map = proto_mgr.mutable_operation_to_metric_map();
  proto_op_map->Clear();

  for (const auto& [op_id, metric_ptr] : op_map) {
    if (!metric_ptr) {
      ASTL_LOG_ERROR("Serialize: null metric_ptr for op {}", op_id);
      return ASTL_STATUS_INTERNAL_ERROR;
    }

    auto target_or = metric_manager.GetTargetForMetric(metric_ptr);
    if (!target_or) {
      ASTL_LOG_ERROR("Serialize: cannot find target for metric '{}' (op {})", metric_ptr->Name(), op_id);
      return target_or.error();
    }

    const ITarget* target = *target_or;

    ASTL_LOG_DEBUG("Serialize: adding op {} -> metric '{}' on target '{}'", op_id, metric_ptr->Name(), target->Name());

    auto* entry = proto_op_map->Add();
    entry->set_operation_id(op_id);
    entry->set_metric_id(metric_ptr->Name());
    entry->set_target_id(target->Name());
  }

  return ASTL_STATUS_SUCCESS;
}

static auto DeserializeMetricHandle(const astl::protobuf::RawMetric&             raw,
                                    const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<MetricHandle, astl_status_code> {
  MetricHandle metric_handle{};

  const auto& raw_cfg     = raw.config();
  const auto& metric_name = raw_cfg.metric_name();
  const auto  metric_type = static_cast<astl_metric_type_t>(raw_cfg.metric_type());

  auto cfg_and_results_or_err = DeserializeMetricForType(metric_type, raw, targets);
  if (!cfg_and_results_or_err) {
    ASTL_LOG_ERROR("DeserializeMetricHandle: DeserializeMetricForType failed for metric {}", raw.metric_id());
    return std::unexpected(cfg_and_results_or_err.error());
  }

  // take ownership of config
  auto& [config_unique_ptr, results] = cfg_and_results_or_err.value();
  metric_handle.config               = std::move(config_unique_ptr);

  // move metrics into the handle
  for (auto& res : results) {
    const ITarget* target = res.target;
    auto&          metric = res.metric;

    if (!target) {
      ASTL_LOG_ERROR("DeserializeMetricHandle: Deserialized metric has null target for metric {}", metric_name);
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }

    auto [it, inserted] = metric_handle.target_to_metric_map.emplace(target, std::move(metric));
    if (!inserted) {
      ASTL_LOG_ERROR("DeserializeMetricHandle: duplicate target '{}' in RawMetric for metric {}", target->Name(),
                     metric_name);
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }
  }

  return metric_handle;
}

static auto BuildCapabilities(const astl::protobuf::MetricManager& proto_manager) -> astl::Capabilities {
  std::vector<astl::CollectorCapability> collector_caps_list;
  collector_caps_list.reserve(static_cast<size_t>(proto_manager.capabilities_size()));

  for (int i = 0; i < proto_manager.capabilities_size(); ++i) {
    auto proto_cap      = proto_manager.capabilities(i);
    auto collector_type = static_cast<CollectorType>(proto_cap);

    if (collector_type == CollectorType::UNKNOWN) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: Skipping UNKNOWN collector type");
      continue;
    }

    collector_caps_list.emplace_back(collector_type);
  }

  // We don't currently use SystemCapabilities when constructing MetricManager
  astl::SystemCapability              system_capabilities{};
  std::vector<astl::SystemCapability> system_caps_list{system_capabilities};
  astl::Capabilities                  caps{std::move(collector_caps_list), std::move(system_caps_list)};
  return caps;
}

struct RebuiltMetricHandles {
  std::vector<std::unique_ptr<MetricHandle>> metric_handles;
  MetricManager::TargetToMetricsMap          target_to_metrics_map;
};

static auto RebuildMetricHandles(const astl::protobuf::MetricManager&         proto_manager,
                                 const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<RebuiltMetricHandles, astl_status_code> {
  RebuiltMetricHandles rebuilt_metric_handles;

  const auto& proto_metrics_vec = proto_manager.metrics();
  rebuilt_metric_handles.metric_handles.reserve(static_cast<std::size_t>(proto_metrics_vec.metrics_size()));

  for (int i = 0; i < proto_metrics_vec.metrics_size(); ++i) {
    const auto& raw = proto_metrics_vec.metrics(i);

    auto handle_or_err = detail::DeserializeMetricHandle(raw, targets);
    if (!handle_or_err) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: DeserializeMetricHandle failed for metric_id '{}'", raw.metric_id());
      return std::unexpected(handle_or_err.error());
    }

    auto          handle_unique_ptr = std::make_unique<MetricHandle>(std::move(*handle_or_err));
    MetricHandle* raw_handle        = handle_unique_ptr.get();

    // Populate target_to_metrics_map by retrieving all metric instances from each deserialized handle
    for (const auto& entry : raw_handle->target_to_metric_map) {
      const ITarget* target = entry.first;

      auto insert_result =
          rebuilt_metric_handles.target_to_metrics_map.emplace(target, std::vector<astl_metric_handle_t>{});
      auto& handles_for_target = insert_result.first->second;

      handles_for_target.push_back(raw_handle);
    }

    rebuilt_metric_handles.metric_handles.emplace_back(std::move(handle_unique_ptr));
  }

  return rebuilt_metric_handles;
}

static auto RebuildOperationMap(const astl::protobuf::MetricManager&              proto_manager,
                                const std::vector<std::unique_ptr<MetricHandle>>& metric_handles)
    -> std::expected<MetricManager::OperationToMetricMap, astl_status_code> {
  MetricManager::OperationToMetricMap operation_to_metric_map;

  for (const auto& entry : proto_manager.operation_to_metric_map()) {
    const uint32_t     op_id     = entry.operation_id();
    const std::string& metric_id = entry.metric_id();
    const std::string& target_id = entry.target_id();

    // 1) find the MetricHandle with this metric_id
    const auto handle_it =
        std::find_if(metric_handles.begin(), metric_handles.end(), [&](const std::unique_ptr<MetricHandle>& handle) {
          return handle && handle->config && handle->config->Name() == metric_id;
        });

    if (handle_it == metric_handles.end()) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: op_id {} refers to unknown metric_id '{}'", op_id, metric_id);
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    MetricHandle* handle = handle_it->get();

    // 2) within that handle, find the metric for the target with target_id
    const auto target_it =
        std::find_if(handle->target_to_metric_map.begin(), handle->target_to_metric_map.end(), [&](const auto& pair) {
          const ITarget* target = pair.first;
          return target && target->Name() == target_id;
        });

    if (target_it == handle->target_to_metric_map.end() || !target_it->second) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: op_id {} refers to unknown (metric_id='{}', target_id='{}')", op_id,
                     metric_id, target_id);
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    IMetric* found_metric = target_it->second.get();

    ASTL_LOG_DEBUG("RebuildOperationMap: mapping operation id {} to metric '{}' on target '{}'", op_id, metric_id,
                   target_id);

    operation_to_metric_map.emplace(op_id, found_metric);
  }

  return operation_to_metric_map;
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

  auto raw_or_err = detail::SerializeHandleToRawMetric(handle);
  if (!raw_or_err) {
    return raw_or_err.error();
  }

  astl::protobuf::RawMetricVec vec{};
  astl::protobuf::RawMetric    raw = std::move(raw_or_err.value());
  *vec.add_metrics()               = std::move(raw);

  if (!vec.SerializeToOstream(&output_stream)) {
    ASTL_LOG_ERROR("Serialize(MetricHandle): Failed to serialize RawMetricVec for metric '{}'", handle.config->Name());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

template <>
auto Deserialize<std::vector<std::unique_ptr<MetricHandle>>>(std::istream&                                input,
                                                             const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<std::vector<std::unique_ptr<MetricHandle>>, astl_status_code> {
  astl::protobuf::RawMetricVec proto_metrics;
  if (!proto_metrics.ParseFromIstream(&input)) {
    ASTL_LOG_ERROR("Deserialize<vector<MetricHandle>>: Failed to parse RawMetricVec");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  std::vector<std::unique_ptr<MetricHandle>> handles;
  handles.reserve(static_cast<decltype(handles)::size_type>(proto_metrics.metrics_size()));

  for (const auto& raw : proto_metrics.metrics()) {
    auto handle_or_err = detail::DeserializeMetricHandle(raw, targets);
    if (!handle_or_err) {
      ASTL_LOG_ERROR("Deserialize<vector<MetricHandle>>: Failed to deserialize metric '{}'", raw.metric_id());
      return std::unexpected(handle_or_err.error());
    }

    handles.emplace_back(std::make_unique<MetricHandle>(std::move(*handle_or_err)));
  }

  return handles;
}

auto Serialize(const MetricManager& metric_manager, std::ostream& output_stream) -> astl_status_code {
  astl::protobuf::MetricManager proto_mgr;

  detail::SerializeCollectorCapabilities(metric_manager._capabilities, proto_mgr);

  auto metrics_status = detail::SerializeMetricHandles(metric_manager._metric_handles, proto_mgr);
  if (!metrics_status) {
    return metrics_status.error();
  }

  auto group_status = detail::SerializeMetricGroups(metric_manager._metric_groups, proto_mgr);
  if (group_status != ASTL_STATUS_SUCCESS) {
    return group_status;
  }

  ASTL_LOG_DEBUG("serialize: serializing operation to metric map");
  auto op_status = detail::SerializeOperationToMetricMap(metric_manager._operation_to_metric_map,
                                                         metric_manager,  // so helper can call GetTargetForMetric
                                                         proto_mgr);

  if (op_status != ASTL_STATUS_SUCCESS) {
    return op_status;
  }

  if (!proto_mgr.SerializeToOstream(&output_stream)) {
    ASTL_LOG_ERROR("Serialize(MetricManager): Failed to serialize MetricManager to output stream");
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  return ASTL_STATUS_SUCCESS;
}

// We need to dynamic cast to MetricManager to access internal data structures
// Orchestrator only exposes IMetricManager interface
auto Serialize(const IMetricManager& i_metric_manager, std::ostream& output_stream) -> astl_status_code {
  const auto* concrete_metric_manager = dynamic_cast<const MetricManager*>(std::addressof(i_metric_manager));
  if (!concrete_metric_manager) {
    ASTL_LOG_ERROR("Serialize(IMetricManager): Failed to cast IMetricManager to MetricManager");
    return ASTL_STATUS_BAD_ARGUMENT;
  }

  auto result = Serialize(*concrete_metric_manager, output_stream);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Serialize(IMetricManager): Failed to serialize MetricManager with status {}",
                   astlStatusString(result));
    return result;
  }

  return ASTL_STATUS_SUCCESS;
}

template <>
auto Deserialize<std::unique_ptr<MetricManager>>(std::istream&                                input_stream,
                                                 const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<std::unique_ptr<MetricManager>, astl_status_code> {
  astl::protobuf::MetricManager proto_manager;
  if (!proto_manager.ParseFromIstream(&input_stream)) {
    ASTL_LOG_ERROR("Deserialize<MetricManager>: Failed to parse MetricManager from input stream");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  auto caps           = detail::BuildCapabilities(proto_manager);
  auto metric_manager = std::make_unique<MetricManager>(std::move(caps));

  // TODO(ASTL-408) counters: not serialized yet
  metric_manager->_counter_handles.clear();
  metric_manager->_target_to_counters_map.clear();

  auto rebuilt_metrics_or_err = detail::RebuildMetricHandles(proto_manager, targets);
  if (!rebuilt_metrics_or_err) {
    return std::unexpected(rebuilt_metrics_or_err.error());
  }

  auto& rebuilt_metrics = *rebuilt_metrics_or_err;
  ASTL_LOG_DEBUG("Deserialize<MetricManager>: rebuilt {} metric handles", rebuilt_metrics.metric_handles.size());
  metric_manager->_metric_handles.swap(rebuilt_metrics.metric_handles);
  metric_manager->_target_to_metrics_map.swap(rebuilt_metrics.target_to_metrics_map);

  // metric groups
  metric_manager->_metric_groups.clear();
  metric_manager->_metric_group_api_handles.clear();
  for (auto& handle_ptr : metric_manager->_metric_handles) {
    const MetricConfig* cfg = handle_ptr->config.get();
    if (!cfg || cfg->MetricGroups().empty()) {
      continue;
    }

    std::vector<const ITarget*> local_targets{};
    local_targets.reserve(handle_ptr->target_to_metric_map.size());
    for (auto& [target, metric] : handle_ptr->target_to_metric_map) {
      local_targets.push_back(target);
    }

    auto status = metric_manager->AddMetricToGroups(handle_ptr.get(), cfg, local_targets);
    if (status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }
  }

  auto op_map_or_err = detail::RebuildOperationMap(proto_manager, metric_manager->_metric_handles);
  if (!op_map_or_err) {
    return std::unexpected(op_map_or_err.error());
  }
  metric_manager->_operation_to_metric_map.swap(*op_map_or_err);

  // set all metric's processed sample sink to metric_manager
  for (const auto& handle_ptr : metric_manager->_metric_handles) {
    for (const auto& [target, metric_ptr] : handle_ptr->target_to_metric_map) {
      metric_ptr->SetProcessedSampleSink(metric_manager.get());
    }
  }

  return metric_manager;
}

// Similar to Serialize(IMetricManager), we need to build a concrete MetricManager,
// but return it as a unique_ptr<IMetricManager>
template <>
auto Deserialize<std::unique_ptr<IMetricManager>>(std::istream&                                input_stream,
                                                  const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<std::unique_ptr<IMetricManager>, astl_status_code> {
  auto metric_manager = Deserialize<std::unique_ptr<MetricManager>>(input_stream, targets);

  if (!metric_manager) {
    return std::unexpected(metric_manager.error());
  }

  return std::unique_ptr<IMetricManager>(std::move(metric_manager.value()));
}

}  // namespace astl::ProtobufSerDes
