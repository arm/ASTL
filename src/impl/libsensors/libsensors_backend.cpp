// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/collector_backend.hpp"
#include "collector/i_collector.hpp"
#include "libsensors/libsensors_target.hpp"
#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "libsensors/libsensors_collector.hpp"
#  include "libsensors/libsensors_metric_builder.hpp"
#  include "libsensors/libsensors_topology_plugin.hpp"
#endif
namespace astl {
namespace {
auto BuildLibsensorsCollector(const ITarget& target, const AstlConfiguration& /*configuration*/)
    -> std::expected<std::unique_ptr<ICollector>, astl_status_code> {
#if defined(ASTL_INCLUDE_LIBSENSORS)
  if (const auto* sensors = dynamic_cast<const LibsensorsTarget*>(&target)) {
    return std::make_unique<LibsensorsCollector>(sensors->ShareApi());
  }
#else
  (void)target;
#endif
  return nullptr;
}
}  // namespace
extern const CollectorBackend kLibsensorsBackend{CollectorType::LIBSENSORS, "libsensors",
#if defined(ASTL_INCLUDE_LIBSENSORS)
                                                 LibsensorsTopologyPlugin::ScanForTargets, BuildLibsensorsCollector,
                                                 RegisterLibsensorsMetrics
#else
                                                 nullptr, BuildLibsensorsCollector, nullptr
#endif
};
}  // namespace astl
