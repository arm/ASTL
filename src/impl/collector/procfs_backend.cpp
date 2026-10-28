// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/collector_backend.hpp"
#include "collector/i_collector.hpp"
#if defined(ASTL_INCLUDE_PROCFS)
#  include "collector/procfs_collector.hpp"
#  include "metric/procfs_metric_builder.hpp"
#  include "topology/procfs_topology_plugin.hpp"
#endif
namespace astl {
namespace {
auto BuildProcfsCollector(const ITarget& target, const AstlConfiguration& /*configuration*/)
    -> std::expected<std::unique_ptr<ICollector>, astl_status_code> {
#if defined(ASTL_INCLUDE_PROCFS)
  const auto* procfs_target = dynamic_cast<const ProcfsTarget*>(&target);
  return std::make_unique<ProcfsCollector>(
      FileInterface{procfs_target ? procfs_target->ProcfsRootPath() : procfs::kDefaultProcfsRootPath});
#else
  (void)target;
  return std::unexpected(ASTL_STATUS_NOT_SUPPORTED);
#endif
}
#if defined(ASTL_INCLUDE_PROCFS)
auto Register(const AstlConfiguration& config, const BackendTargetMap& targets, IMetricManager* manager)
    -> astl_status_code {
  const auto status = RegisterProcfsMetrics(config, targets, manager);
  return status == ASTL_STATUS_SUCCESS ? RegisterProcfsCounters(config, targets, manager) : status;
}
#endif
}  // namespace
extern const CollectorBackend kProcfsBackend{CollectorType::PROCFS, "procfs",
#if defined(ASTL_INCLUDE_PROCFS)
                                             ProcfsTopologyPlugin::ScanForTargets, BuildProcfsCollector, Register
#else
                                             nullptr, BuildProcfsCollector, nullptr
#endif
};
}  // namespace astl
