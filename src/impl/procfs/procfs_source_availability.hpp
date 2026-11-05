// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_SOURCE_AVAILABILITY_HPP_
#define PROCFS_SOURCE_AVAILABILITY_HPP_

#include "common/metric_config.hpp"
#include "procfs/procfs_metric_json_declaration.hpp"
#include "procfs/procfs_utils.hpp"

namespace astl::procfs_metric_builder_helpers::detail {
class ProcfsSourceAvailability {
 public:
  static auto Create(const FileInterface& files, const metrics::spec::ProcfsMetricJsonCollectionSettings& settings)
      -> std::optional<ProcfsSourceAvailability>;
  auto SupportsCpuLabel(const std::string& label) const -> bool;
  auto SupportsMetric(const MetricConfig& config, bool is_composite, const std::string& label) const
      -> std::expected<bool, astl_status_code>;

 private:
  ProcfsSourceAvailability(const FileInterface&                                     files,
                           const metrics::spec::ProcfsMetricJsonCollectionSettings& settings);
  auto IsSystem() const -> bool;
  auto ReadSnapshots() -> bool;

  const FileInterface*                                     _files;
  const metrics::spec::ProcfsMetricJsonCollectionSettings* _settings;
  std::string                                              _field_type;
  std::optional<procfs::InterruptCounterMap>               _interrupt_counters;
  std::optional<procfs::CpuSnapshotMap>                    _iowait_snapshots;
};
}  // namespace astl::procfs_metric_builder_helpers::detail
#endif  // PROCFS_SOURCE_AVAILABILITY_HPP_
