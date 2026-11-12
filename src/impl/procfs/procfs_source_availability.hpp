// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_SOURCE_AVAILABILITY_HPP_
#define PROCFS_SOURCE_AVAILABILITY_HPP_

#include "astl_cxx_compat.hpp"
#include "common/metric_config.hpp"
#include "procfs/procfs_metric_json_declaration.hpp"
#include "procfs/procfs_utils.hpp"

namespace astl::procfs_metric_builder_helpers::detail {
class ProcfsSourceAvailability {
 public:
  static auto Create(const FileInterface& files, const metrics::spec::ProcfsMetricJsonCollectionSettings& settings)
      -> astl::compat::optional<ProcfsSourceAvailability>;
  auto SupportsCpuLabel(const std::string& label) const -> bool;
  auto SupportsMetric(const MetricConfig& config, bool is_composite, const std::string& label) const
      -> astl::compat::expected<bool, astl_status_code>;

 private:
  ProcfsSourceAvailability(const FileInterface&                                     files,
                           const metrics::spec::ProcfsMetricJsonCollectionSettings& settings);
  auto IsSystem() const -> bool;
  auto ReadSnapshots() -> bool;

  const FileInterface*                                     _files;
  const metrics::spec::ProcfsMetricJsonCollectionSettings* _settings;
  std::string                                              _field_type;
  astl::compat::optional<procfs::InterruptCounterMap>      _interrupt_counters;
  astl::compat::optional<procfs::CpuSnapshotMap>           _iowait_snapshots;
};
}  // namespace astl::procfs_metric_builder_helpers::detail
#endif  // PROCFS_SOURCE_AVAILABILITY_HPP_
