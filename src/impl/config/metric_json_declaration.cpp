// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "config/metric_json_declaration.hpp"

#include <optional>

#include "astl_utils.hpp"

namespace astl::metrics::spec {

auto ParseCollectorType(const MetricJsonDeclaration& metric_declaration) -> std::optional<CollectorType> {
  auto collector_type_lower = astl::ToLowerCopy(metric_declaration.collection.protocol);
  if (collector_type_lower == "scmi") {
    return CollectorType::SCMI;
  }
  if (collector_type_lower == "libsensors") {
    return CollectorType::LIBSENSORS;
  }
  if (collector_type_lower == "procfs") {
    return CollectorType::PROCFS;
  }
  return std::nullopt;
}

}  // namespace astl::metrics::spec
