// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <format>
#include <string_view>
#include <type_traits>

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "astl_logger.hpp"
#include "common/string_pool.hpp"
#include "metric/delta_metric.hpp"
#include "metric/event_metric.hpp"
#include "metric/finite_set_metric.hpp"
#include "metric/formula_builder.hpp"
#include "metric/metric_manager.hpp"
#include "metric/rate_metric.hpp"
#include "metric/sampled_value_metric.hpp"
#include "serdes/metrics.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.
#include "serdes/protobuf_serdes.hpp"
#include "serdes/serdes_metrics_detail.hpp"
#include "serdes/serdes_util.hpp"

namespace astl::ProtobufSerDes {

namespace detail {

template <typename>
inline constexpr bool kAlwaysFalse = false;

static auto ToProtoUnits(astl_units_t units) -> astl::protobuf::AstlUnits {
  return units == ASTL_UNITS_UNKNOWN ? astl::protobuf::ASTL_UNITS_UNKNOWN_PROTO
                                     : static_cast<astl::protobuf::AstlUnits>(units);
}

static auto ToProtoValueType(astl_value_type_t value_type) -> astl::protobuf::AstlValueType {
  return value_type == ASTL_VALUE_UNKNOWN ? astl::protobuf::ASTL_VALUE_UNKNOWN_PROTO
                                          : static_cast<astl::protobuf::AstlValueType>(value_type);
}

static auto ToProtoMetricType(astl_metric_type_t metric_type) -> astl::protobuf::AstlMetricType {
  return metric_type == ASTL_METRIC_UNKNOWN ? astl::protobuf::ASTL_METRIC_UNKNOWN_PROTO
                                            : static_cast<astl::protobuf::AstlMetricType>(metric_type);
}

static auto ToProtoMetricIdentifier(astl_metric_identifier_t identifier) -> astl::protobuf::AstlMetricIdentifier {
  return identifier == ASTL_METRIC_IDENTIFIER_UNKNOWN ? astl::protobuf::ASTL_METRIC_IDENTIFIER_UNKNOWN_PROTO
                                                      : static_cast<astl::protobuf::AstlMetricIdentifier>(identifier);
}

static auto ToProtoCollectorType(CollectorType collector_type) -> astl::protobuf::CollectorType {
  return collector_type == CollectorType::UNKNOWN ? astl::protobuf::COLLECTOR_TYPE_UNKNOWN
                                                  : static_cast<astl::protobuf::CollectorType>(collector_type);
}

static auto FromProtoUnits(astl::protobuf::AstlUnits units) -> astl_units_t {
  return units == astl::protobuf::ASTL_UNITS_UNKNOWN_PROTO ? ASTL_UNITS_UNKNOWN : static_cast<astl_units_t>(units);
}

static auto FromProtoValueType(astl::protobuf::AstlValueType value_type) -> astl_value_type_t {
  return value_type == astl::protobuf::ASTL_VALUE_UNKNOWN_PROTO ? ASTL_VALUE_UNKNOWN
                                                                : static_cast<astl_value_type_t>(value_type);
}

static auto FromProtoMetricIdentifier(astl::protobuf::AstlMetricIdentifier identifier) -> astl_metric_identifier_t {
  return identifier == astl::protobuf::ASTL_METRIC_IDENTIFIER_UNKNOWN_PROTO
             ? ASTL_METRIC_IDENTIFIER_UNKNOWN
             : static_cast<astl_metric_identifier_t>(identifier);
}

static auto FromProtoMetricType(astl::protobuf::AstlMetricType metric_type) -> astl_metric_type_t {
  return metric_type == astl::protobuf::ASTL_METRIC_UNKNOWN_PROTO ? ASTL_METRIC_UNKNOWN
                                                                  : static_cast<astl_metric_type_t>(metric_type);
}

static auto FromProtoCollectorType(astl::protobuf::CollectorType collector_type) -> CollectorType {
  return collector_type == astl::protobuf::COLLECTOR_TYPE_UNKNOWN ? CollectorType::UNKNOWN
                                                                  : static_cast<CollectorType>(collector_type);
}

static void SerializeFormulaStep(const FormulaPipeline::PipelineStep& step, astl::protobuf::FormulaStep* out) {
  std::visit(
      [out](const auto& impl) {
        using T = std::decay_t<decltype(impl)>;
        if constexpr (std::is_same_v<T, IdentityFormula>) {
          out->set_identity(true);
        } else if constexpr (std::is_same_v<T, ScalingFormula>) {
          auto* scaling = out->mutable_scaling();
          scaling->set_numerator(impl.GetNumerator());
          scaling->set_denominator(impl.GetDenominator());
        } else if constexpr (std::is_same_v<T, ExpressionFormula>) {
          out->set_expression(std::string{impl.GetExpression()});
        } else {
          static_assert(kAlwaysFalse<T>, "Unhandled formula pipeline step type");
        }
      },
      step);
}

static void SerializeFormula(const AnyFormula& formula, astl::protobuf::MetricFormula* out) {
  std::visit(
      [out](const auto& impl) {
        using T = std::decay_t<decltype(impl)>;
        if constexpr (std::is_same_v<T, IdentityFormula>) {
          out->mutable_identity();
        } else if constexpr (std::is_same_v<T, ScalingFormula>) {
          auto* scaling = out->mutable_scaling();
          scaling->set_numerator(impl.GetNumerator());
          scaling->set_denominator(impl.GetDenominator());
        } else if constexpr (std::is_same_v<T, ExpressionFormula>) {
          out->mutable_expression()->set_expression(std::string{impl.GetExpression()});
        } else if constexpr (std::is_same_v<T, FormulaPipeline>) {
          auto* pipeline = out->mutable_pipeline();
          for (const auto& step : impl.Steps()) {
            SerializeFormulaStep(step, pipeline->add_steps());
          }
        } else {
          static_assert(kAlwaysFalse<T>, "Unhandled formula type");
        }
      },
      formula);
}

static auto DeserializeFormulaStep(const astl::protobuf::FormulaStep& step)
    -> std::expected<FormulaPipeline::PipelineStep, astl_status_code> {
  switch (step.step_case()) {
    case astl::protobuf::FormulaStep::kIdentity:
      return FormulaPipeline::PipelineStep{IdentityFormula{}};
    case astl::protobuf::FormulaStep::kScaling: {
      const auto& scaling = step.scaling();
      return FormulaPipeline::PipelineStep{
          ScalingFormula{scaling.numerator(), scaling.denominator()}
      };
    }
    case astl::protobuf::FormulaStep::kExpression: {
      auto expression_or_err = ExpressionFormula::Create(step.expression());
      if (!expression_or_err) {
        return std::unexpected(expression_or_err.error());
      }
      return FormulaPipeline::PipelineStep{std::move(expression_or_err.value())};
    }
    case astl::protobuf::FormulaStep::STEP_NOT_SET:
    default:
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

static auto DeserializeFormula(const astl::protobuf::MetricFormula& serialized_formula)
    -> std::expected<AnyFormula, astl_status_code> {
  switch (serialized_formula.formula_case()) {
    case astl::protobuf::MetricFormula::kIdentity:
      return AnyFormula{IdentityFormula{}};
    case astl::protobuf::MetricFormula::kScaling: {
      const auto& scaling = serialized_formula.scaling();
      return AnyFormula{
          ScalingFormula{scaling.numerator(), scaling.denominator()}
      };
    }
    case astl::protobuf::MetricFormula::kExpression: {
      auto expression_or_err = ExpressionFormula::Create(serialized_formula.expression().expression());
      if (!expression_or_err) {
        return std::unexpected(expression_or_err.error());
      }
      return AnyFormula{std::move(expression_or_err.value())};
    }
    case astl::protobuf::MetricFormula::kPipeline: {
      std::vector<FormulaPipeline::PipelineStep> steps;
      const auto&                                pipeline = serialized_formula.pipeline();
      steps.reserve(static_cast<size_t>(pipeline.steps_size()));
      for (const auto& step : pipeline.steps()) {
        auto step_or_err = DeserializeFormulaStep(step);
        if (!step_or_err) {
          return std::unexpected(step_or_err.error());
        }
        steps.push_back(std::move(step_or_err.value()));
      }
      return steps.empty() ? AnyFormula{IdentityFormula{}} : AnyFormula{FormulaPipeline{std::move(steps)}};
    }
    case astl::protobuf::MetricFormula::FORMULA_NOT_SET:
    default:
      return AnyFormula{IdentityFormula{}};
  }
}

/**
 * @brief Serializes a MetricConfig into a protobuf representation.
 *
 * Converts a MetricConfig object into its corresponding protobuf message format,
 * including metric metadata (name, units, type, identifier) and associated metric groups.
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
auto SerializeBasicMetricConfig(const MetricConfig& config)
    -> std::expected<astl::protobuf::MetricConfig, astl_status_code> {
  astl::protobuf::MetricConfig out;

  out.set_metric_name(config.Name());
  out.set_description(config.Description());
  out.set_units(ToProtoUnits(config.Units()));
  out.set_value_type(ToProtoValueType(config.ValueType()));
  out.set_input_value_type(ToProtoValueType(config.InputValueType()));
  out.set_metric_type(ToProtoMetricType(config.MetricType()));
  out.set_identifier(ToProtoMetricIdentifier(config.Identifier()));

  for (const auto& group : config.MetricGroups()) {
    out.add_metric_groups(group);
  }

  out.set_collector_type(ToProtoCollectorType(config.GetCollectorType()));
  SerializeFormula(config.GetFormula(), out.mutable_formula());

  return out;
}

static auto SerializeBasicMetric(const MetricConfig& metric_config, const ITarget* target)
    -> std::expected<astl::protobuf::RawMetric, astl_status_code> {
  astl::protobuf::RawMetric out;
  if (!target) {
    ASTL_LOG_ERROR("SerializeBasicMetric: target is null for metric {}", metric_config.Name());
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  out.set_metric_id(metric_config.Id());
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

  for (const auto& [value, state_info] : finite_set_config.GetStateInfo()) {
    const int64_t key     = *value.ToInt64();
    auto&         pb_info = (*finite_set_msg->mutable_value_to_state_info())[key];
    pb_info.set_label(state_info.state_name);
    pb_info.set_description(state_info.state_description);
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

auto DeserializeBasicMetricConfig(const astl::protobuf::MetricConfig& proto_cfg, const std::string& metric_id)
    -> std::expected<std::unique_ptr<MetricConfig>, astl_status_code> {
  const std::string& name        = proto_cfg.metric_name();
  const std::string& description = proto_cfg.description();

  const auto units            = FromProtoUnits(proto_cfg.units());
  const auto value_type       = FromProtoValueType(proto_cfg.value_type());
  const auto input_value_type = FromProtoValueType(proto_cfg.input_value_type());
  const auto identifier       = FromProtoMetricIdentifier(proto_cfg.identifier());
  const auto metric_type      = FromProtoMetricType(proto_cfg.metric_type());
  const auto collector        = FromProtoCollectorType(proto_cfg.collector_type());
  AnyFormula formula          = IdentityFormula{};
  if (proto_cfg.has_formula()) {
    auto formula_or_err = DeserializeFormula(proto_cfg.formula());
    if (!formula_or_err) {
      ASTL_LOG_ERROR("DeserializeBasicMetricConfig: failed to deserialize formula for metric {}", metric_id);
      return std::unexpected(formula_or_err.error());
    }
    formula = std::move(formula_or_err.value());
  }

  if (proto_cfg.metric_groups_size() == 0) {
    auto cfg = std::make_unique<MetricConfig>(name, description, units, value_type, identifier, metric_type, collector,
                                              NullOperationBuilder{}, std::move(formula), input_value_type,
                                              std::vector<std::string>{}, metric_id);
    return cfg;
  }

  std::vector<std::string> groups{proto_cfg.metric_groups().begin(), proto_cfg.metric_groups().end()};
  auto cfg = std::make_unique<MetricConfig>(name, description, units, value_type, identifier, metric_type, collector,
                                            NullOperationBuilder{}, std::move(formula), input_value_type,
                                            std::move(groups), metric_id);
  return cfg;
}

static auto DeserializeFiniteSetMetricConfig(const astl::protobuf::MetricConfig& proto_cfg,
                                             const std::string&                  metric_id)
    -> std::expected<std::unique_ptr<FiniteSetMetricConfig>, astl_status_code> {
  if (!proto_cfg.has_finite_set()) {
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  const auto&        finite_set_cfg_proto = proto_cfg.finite_set();
  const std::string& name                 = proto_cfg.metric_name();
  const std::string& description          = proto_cfg.description();

  const auto               units            = FromProtoUnits(proto_cfg.units());
  const auto               value_type       = FromProtoValueType(proto_cfg.value_type());
  const auto               input_value_type = FromProtoValueType(proto_cfg.input_value_type());
  const auto               identifier       = FromProtoMetricIdentifier(proto_cfg.identifier());
  const auto               metric_type      = FromProtoMetricType(proto_cfg.metric_type());
  const auto               collector        = FromProtoCollectorType(proto_cfg.collector_type());
  std::vector<std::string> metric_groups{proto_cfg.metric_groups().begin(), proto_cfg.metric_groups().end()};

  FiniteSetMetricConfig::FiniteSet      finite_set;
  FiniteSetMetricConfig::ValueToInfoMap state_info;
  try {
    for (const auto& [key_i64, pb_info] : finite_set_cfg_proto.value_to_state_info()) {
      const auto key = static_cast<uint64_t>(key_i64);
      finite_set.emplace(key);
      state_info.emplace(AstlValue(key), FiniteSetMetricConfig::StateInfo{pb_info.label(), pb_info.description()});
    }
  } catch (const std::bad_variant_access& e) {
    ASTL_LOG_ERROR("DeserializeFiniteSetMetricConfig: bad variant access when parsing finite set for metric {}: {}",
                   name, e.what());
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  AnyFormula formula = IdentityFormula{};
  if (proto_cfg.has_formula()) {
    auto formula_or_err = DeserializeFormula(proto_cfg.formula());
    if (!formula_or_err) {
      ASTL_LOG_ERROR("DeserializeFiniteSetMetricConfig: failed to deserialize formula for metric {}", metric_id);
      return std::unexpected(formula_or_err.error());
    }
    formula = std::move(formula_or_err.value());
  }
  auto cfg = std::make_unique<FiniteSetMetricConfig>(name, description, units, value_type, metric_type, identifier,
                                                     collector, NullOperationBuilder{}, std::move(finite_set),
                                                     std::move(state_info), std::move(formula), input_value_type,
                                                     std::move(metric_groups), metric_id);
  return cfg;
}

struct MetricDeserializationResult {
  MetricDeserializationResult(const ITarget* target_in, std::unique_ptr<IMetric> metric_in)
      : target(target_in), metric(std::move(metric_in)) {}

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

    result.emplace_back(target, std::move(metric));
  }

  return result;
}

static auto DeserializeMetricForType(astl_metric_type_t metric_type, const astl::protobuf::RawMetric& raw,
                                     const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<MetricConfigAndResults, astl_status_code> {
  switch (metric_type) {
    case ASTL_METRIC_VALUE: {
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config(), raw.metric_id());
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
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config(), raw.metric_id());
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
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config(), raw.metric_id());
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
      auto cfg_or_err = DeserializeBasicMetricConfig(raw.config(), raw.metric_id());
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
      auto cfg_or_err = DeserializeFiniteSetMetricConfig(raw.config(), raw.metric_id());
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
    proto_mgr.add_capabilities(cap.collector_type == CollectorType::UNKNOWN
                                   ? astl::protobuf::COLLECTOR_TYPE_UNKNOWN
                                   : static_cast<astl::protobuf::CollectorType>(cap.collector_type));
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

auto SerializeOperationToMetricMap(const MetricManager::TargetOperationToMetricMap& target_to_op_map,
                                   astl::protobuf::MetricManager&                   proto_mgr) -> astl_status_code {
  auto* proto_op_map = proto_mgr.mutable_operation_to_metric_map();
  proto_op_map->Clear();

  for (const auto& [target, op_map] : target_to_op_map) {
    if (!target) {
      ASTL_LOG_ERROR("Serialize: null target in operation routing map");
      return ASTL_STATUS_INTERNAL_ERROR;
    }

    for (const auto& [op_id, metric_ptr] : op_map) {
      if (!metric_ptr) {
        ASTL_LOG_ERROR("Serialize: null metric_ptr for op {} on target '{}'", op_id, target->Name());
        return ASTL_STATUS_INTERNAL_ERROR;
      }

      ASTL_LOG_DEBUG("Serialize: adding op {} -> metric '{}' on target '{}'", op_id, metric_ptr->Id(), target->Name());

      auto* entry = proto_op_map->Add();
      entry->set_operation_id(op_id);
      entry->set_metric_id(metric_ptr->Id());
      entry->set_target_id(target->Name());
    }
  }

  return ASTL_STATUS_SUCCESS;
}

auto SerializeClockCorrelations(const ClockCorrelationMap& correlations, astl::protobuf::MetricManager& proto_mgr)
    -> void {
  auto* proto_list = proto_mgr.mutable_clock_correlations();
  proto_list->Clear();

  for (const auto& [op_id, corr] : correlations) {
    auto* entry = proto_list->Add();
    entry->set_operation_id(op_id);

    auto* proto_corr = entry->mutable_correlation();
    proto_corr->set_raw_monotonic_at_start_ns(corr.raw_monotonic_at_start.time_since_epoch().count());
    proto_corr->set_native_at_start_ticks(corr.native_at_start);

    auto* proto_ratio = proto_corr->mutable_ticks();
    proto_ratio->set_num(corr.ticks.num);
    proto_ratio->set_den(corr.ticks.den);
  }
}

static auto DeserializeClockCorrelations(const astl::protobuf::MetricManager& proto_mgr) -> ClockCorrelationMap {
  ClockCorrelationMap result;
  result.reserve(static_cast<size_t>(proto_mgr.clock_correlations_size()));

  for (const auto& entry : proto_mgr.clock_correlations()) {
    const auto& proto_corr  = entry.correlation();
    const auto& proto_ratio = proto_corr.ticks();

    OperationClockCorrelation corr{
        ProcessedSampleTimestamp{std::chrono::duration<int64_t, std::nano>{proto_corr.raw_monotonic_at_start_ns()}},
        proto_corr.native_at_start_ticks(), NativeToMonotonicRawRatio{proto_ratio.num(),             proto_ratio.den()                                 }
    };

    auto operation_id_or_error = DeserializeOperationId(entry.operation_id(), "Clock correlation");
    if (!operation_id_or_error.has_value()) {
      continue;
    }
    result[*operation_id_or_error] = corr;
  }

  return result;
}

static auto DeserializeMetricHandle(const astl::protobuf::RawMetric&             raw,
                                    const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<MetricHandle, astl_status_code> {
  MetricHandle metric_handle{};

  const auto& raw_cfg     = raw.config();
  const auto& metric_name = raw_cfg.metric_name();
  const auto  metric_type = raw_cfg.metric_type() == astl::protobuf::ASTL_METRIC_UNKNOWN_PROTO
                                ? ASTL_METRIC_UNKNOWN
                                : static_cast<astl_metric_type_t>(raw_cfg.metric_type());

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
    auto collector_type = proto_cap == astl::protobuf::COLLECTOR_TYPE_UNKNOWN ? CollectorType::UNKNOWN
                                                                              : static_cast<CollectorType>(proto_cap);

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
  RebuiltMetricHandles(std::vector<std::unique_ptr<MetricHandle>> metric_handles_in,
                       MetricManager::TargetToMetricsMap          target_to_metrics_map_in)
      : metric_handles(std::move(metric_handles_in)), target_to_metrics_map(std::move(target_to_metrics_map_in)) {}

  std::vector<std::unique_ptr<MetricHandle>> metric_handles;
  MetricManager::TargetToMetricsMap          target_to_metrics_map;
};

static auto RebuildMetricHandles(const astl::protobuf::MetricManager&         proto_manager,
                                 const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<RebuiltMetricHandles, astl_status_code> {
  RebuiltMetricHandles            rebuilt_metric_handles{{}, {}};
  std::unordered_set<std::string> seen_metric_ids;

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
    if (!raw_handle->config) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: deserialized metric handle has null config");
      return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
    }
    if (!seen_metric_ids.insert(raw_handle->config->Id()).second) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: duplicate metric_id '{}'", raw_handle->config->Id());
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

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

static auto FindMetricForOperationMapEntry(const std::vector<std::unique_ptr<MetricHandle>>&  metric_handles,
                                           const std::vector<std::unique_ptr<CounterHandle>>& counter_handles,
                                           const std::string& metric_id, const std::string& target_id)
    -> std::expected<std::pair<const ITarget*, IMetric*>, astl_status_code> {
  const auto metric_handle_it =
      std::find_if(metric_handles.begin(), metric_handles.end(), [&](const std::unique_ptr<MetricHandle>& handle) {
        return handle && handle->config && handle->config->Id() == metric_id;
      });

  if (metric_handle_it != metric_handles.end()) {
    MetricHandle* handle = metric_handle_it->get();
    const auto    target_it =
        std::find_if(handle->target_to_metric_map.begin(), handle->target_to_metric_map.end(), [&](const auto& pair) {
          const ITarget* target = pair.first;
          return target && target->Name() == target_id;
        });

    if (target_it == handle->target_to_metric_map.end() || !target_it->second) {
      ASTL_LOG_ERROR(
          "Deserialize<MetricManager>: operation map refers to unknown metric target (metric_id='{}', "
          "target_id='{}')",
          metric_id, target_id);
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }

    return std::pair<const ITarget*, IMetric*>{target_it->first, target_it->second.get()};
  }

  const auto counter_handle_it =
      std::find_if(counter_handles.begin(), counter_handles.end(), [&](const std::unique_ptr<CounterHandle>& handle) {
        return handle && handle->config && handle->config->Id() == metric_id;
      });

  if (counter_handle_it == counter_handles.end()) {
    ASTL_LOG_ERROR("Deserialize<MetricManager>: operation map refers to unknown metric_id '{}'", metric_id);
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  CounterHandle* handle = counter_handle_it->get();
  const auto     target_it =
      std::find_if(handle->target_to_counter_map.begin(), handle->target_to_counter_map.end(), [&](const auto& pair) {
        const ITarget* target = pair.first;
        return target && target->Name() == target_id;
      });

  if (target_it == handle->target_to_counter_map.end() || !target_it->second) {
    ASTL_LOG_ERROR(
        "Deserialize<MetricManager>: operation map refers to unknown counter target (metric_id='{}', "
        "target_id='{}')",
        metric_id, target_id);
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  return std::pair<const ITarget*, IMetric*>{target_it->first, target_it->second.get()};
}

static auto RebuildOperationMap(const astl::protobuf::MetricManager&               proto_manager,
                                const std::vector<std::unique_ptr<MetricHandle>>&  metric_handles,
                                const std::vector<std::unique_ptr<CounterHandle>>& counter_handles)
    -> std::expected<MetricManager::TargetOperationToMetricMap, astl_status_code> {
  MetricManager::TargetOperationToMetricMap target_to_operation_to_metric_map;

  for (const auto& entry : proto_manager.operation_to_metric_map()) {
    const uint32_t     op_id     = entry.operation_id();
    const std::string& metric_id = entry.metric_id();
    const std::string& target_id = entry.target_id();

    auto metric_or_err = FindMetricForOperationMapEntry(metric_handles, counter_handles, metric_id, target_id);
    if (!metric_or_err) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: op_id {} could not be resolved", op_id);
      return std::unexpected(metric_or_err.error());
    }

    const auto& [target, found_metric] = *metric_or_err;

    ASTL_LOG_DEBUG("RebuildOperationMap: mapping operation id {} to metric '{}' on target '{}'", op_id, metric_id,
                   target_id);

    auto& op_map = target_to_operation_to_metric_map[target];
    if (!op_map.emplace(op_id, found_metric).second) {
      ASTL_LOG_ERROR("Deserialize<MetricManager>: duplicate operation id {} on target '{}'", op_id, target_id);
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
    }
  }

  return target_to_operation_to_metric_map;
}

static auto RebuildMetricGroupDescriptions(const astl::protobuf::MetricManager& proto_manager)
    -> MetricManager::MetricGroupDescriptionMap {
  const auto build_fallback_description = [](std::string_view group_name) -> std::string {
    if (group_name.empty()) {
      return "Metric group";
    }
    return std::format("Metrics in the '{}' group.", group_name);
  };

  MetricManager::MetricGroupDescriptionMap metric_group_descriptions;

  for (const auto& proto_group : proto_manager.metric_groups()) {
    const auto& description = proto_group.group_description();
    metric_group_descriptions.emplace(
        proto_group.group_name(),
        description.empty() ? build_fallback_description(proto_group.group_name()) : description);
  }

  return metric_group_descriptions;
}

template <typename AddMetricToGroups>
static auto RebuildMetricGroups(std::vector<std::unique_ptr<MetricGroup>>&  metric_groups,
                                std::vector<astl_metric_group_handle_t>&    metric_group_api_handles,
                                std::vector<std::unique_ptr<MetricHandle>>& metric_handles,
                                AddMetricToGroups add_metric_to_groups) -> std::expected<void, astl_status_code> {
  metric_groups.clear();
  metric_group_api_handles.clear();
  for (auto& handle_ptr : metric_handles) {
    const MetricConfig* cfg = handle_ptr->config.get();
    if (!cfg || cfg->MetricGroups().empty()) {
      continue;
    }

    std::vector<const ITarget*> local_targets{};
    local_targets.reserve(handle_ptr->target_to_metric_map.size());
    for (auto& [target, metric] : handle_ptr->target_to_metric_map) {
      local_targets.push_back(target);
    }

    auto status = add_metric_to_groups(handle_ptr.get(), cfg, local_targets);
    if (status != ASTL_STATUS_SUCCESS) {
      return std::unexpected(status);
    }
  }

  return {};
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

  auto counters_status = detail::SerializeCounterHandles(metric_manager._counter_handles, proto_mgr);
  if (!counters_status) {
    return counters_status.error();
  }

  auto group_status = detail::SerializeMetricGroups(metric_manager._metric_groups, proto_mgr);
  if (group_status != ASTL_STATUS_SUCCESS) {
    return group_status;
  }

  ASTL_LOG_DEBUG("serialize: serializing operation to metric map");
  auto op_status = detail::SerializeOperationToMetricMap(metric_manager._target_to_operation_to_metric_map, proto_mgr);

  if (op_status != ASTL_STATUS_SUCCESS) {
    return op_status;
  }

  ASTL_LOG_DEBUG("serialize: serializing clock correlations ({} entries)", metric_manager._clock_correlations.size());
  detail::SerializeClockCorrelations(metric_manager._clock_correlations, proto_mgr);

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

  auto rebuilt_metrics_or_err = detail::RebuildMetricHandles(proto_manager, targets);
  if (!rebuilt_metrics_or_err) {
    return std::unexpected(rebuilt_metrics_or_err.error());
  }

  auto& rebuilt_metrics = *rebuilt_metrics_or_err;
  ASTL_LOG_DEBUG("Deserialize<MetricManager>: rebuilt {} metric handles", rebuilt_metrics.metric_handles.size());
  metric_manager->_metric_group_descriptions = detail::RebuildMetricGroupDescriptions(proto_manager);
  metric_manager->_metric_handles.swap(rebuilt_metrics.metric_handles);
  metric_manager->_target_to_metrics_map.swap(rebuilt_metrics.target_to_metrics_map);

  auto rebuilt_counters_or_err = detail::RebuildCounterHandles(proto_manager, targets, metric_manager->_capabilities);
  if (!rebuilt_counters_or_err) {
    return std::unexpected(rebuilt_counters_or_err.error());
  }
  auto& rebuilt_counters = *rebuilt_counters_or_err;
  ASTL_LOG_DEBUG("Deserialize<MetricManager>: rebuilt {} counter handles", rebuilt_counters.counter_handles.size());
  metric_manager->_counter_handles.swap(rebuilt_counters.counter_handles);
  metric_manager->_target_to_counters_map.swap(rebuilt_counters.target_to_counters_map);

  auto metric_groups_status = detail::RebuildMetricGroups(
      metric_manager->_metric_groups, metric_manager->_metric_group_api_handles, metric_manager->_metric_handles,
      [&metric_manager](astl_metric_handle_t metric_handle, const MetricConfig* metric_config,
                        const std::vector<const ITarget*>& local_targets) {
        return metric_manager->AddMetricToGroups(metric_handle, metric_config, local_targets);
      });
  if (!metric_groups_status) {
    return std::unexpected(metric_groups_status.error());
  }

  auto op_map_or_err =
      detail::RebuildOperationMap(proto_manager, metric_manager->_metric_handles, metric_manager->_counter_handles);
  if (!op_map_or_err) {
    return std::unexpected(op_map_or_err.error());
  }
  metric_manager->_target_to_operation_to_metric_map.swap(*op_map_or_err);

  metric_manager->_clock_correlations = detail::DeserializeClockCorrelations(proto_manager);
  ASTL_LOG_DEBUG("Deserialize<MetricManager>: restored {} clock correlations",
                 metric_manager->_clock_correlations.size());

  // set all metric's processed sample sink to metric_manager
  for (const auto& handle_ptr : metric_manager->_metric_handles) {
    for (const auto& [target, metric_ptr] : handle_ptr->target_to_metric_map) {
      metric_ptr->SetProcessedSampleSink(metric_manager.get());
    }
  }
  for (const auto& handle_ptr : metric_manager->_counter_handles) {
    for (const auto& [target, counter_ptr] : handle_ptr->target_to_counter_map) {
      counter_ptr->SetProcessedSampleSink(metric_manager.get());
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
