// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "procfs/procfs_source_availability.hpp"

#include <algorithm>

#include "astl_cxx_compat.hpp"
#include "astl_utils.hpp"
#include "procfs/procfs_composite_metricconfig.hpp"
#include "procfs/procfs_metric_builder_parsing_helpers.hpp"

namespace astl::procfs_metric_builder_helpers::detail {
namespace {
auto ReadableSystemSource(const FileInterface& files, const procfs::FieldDescriptor& field) -> bool {
  const auto* system_field = std::get_if<procfs::SystemField>(&field);
  if (system_field && (system_field->kind == "processes" || system_field->kind == "threads")) {
    // Task churn or an unreadable status file invalidates a collection sample,
    // but does not mean this procfs mount lacks task-count support.
    std::error_code error;
    return std::filesystem::is_directory(files.GetBasePath() / ".", error);
  }
  return procfs::ReadField(files, field).has_value();
}

auto ReadableCompositeInputs(const FileInterface& files, const MetricConfig& config) -> bool {
  const auto* composite = dynamic_cast<const ProcfsCompositeMetricConfig*>(&config);
  if (!composite) {
    return false;
  }
  return std::ranges::all_of(composite->Inputs(), [&](const auto& input) {
    return procfs::ReadField(files, input.field_descriptor).has_value();
  });
}
}  // namespace

ProcfsSourceAvailability::ProcfsSourceAvailability(const FileInterface&                                     files,
                                                   const metrics::spec::ProcfsMetricJsonCollectionSettings& settings)
    : _files{&files}, _settings{&settings}, _field_type{ToLowerCopy(settings.field_type)} {}

auto ProcfsSourceAvailability::Create(const FileInterface&                                     files,
                                      const metrics::spec::ProcfsMetricJsonCollectionSettings& settings)
    -> astl::compat::optional<ProcfsSourceAvailability> {
  ProcfsSourceAvailability availability{files, settings};
  if (!availability.ReadSnapshots()) {
    return astl::compat::nullopt;
  }
  return availability;
}

auto ProcfsSourceAvailability::IsSystem() const -> bool {
  return _field_type == "system" || _field_type == "system_rate";
}

auto ProcfsSourceAvailability::ReadSnapshots() -> bool {
  if (IsSystem() && _settings->field_name == "interrupts") {
    auto counters = procfs::ReadInterruptCounters(*_files, _settings->relative_path);
    if (!counters) {
      return false;
    }
    _interrupt_counters = std::move(*counters);
  }
  if (_field_type == "cpu_iowait") {
    auto snapshots = procfs::ReadCpuSnapshots(*_files, _settings->relative_path);
    if (!snapshots) {
      return false;
    }
    _iowait_snapshots = std::move(*snapshots);
  }
  return true;
}

auto ProcfsSourceAvailability::SupportsCpuLabel(const std::string& label) const -> bool {
  if (!_iowait_snapshots) {
    return true;
  }
  const auto cpu_label = label.empty() ? _settings->line_prefix.value_or("cpu") : label;
  const auto it        = _iowait_snapshots->find(cpu_label);
  return it != _iowait_snapshots->end() && it->second.has_iowait;
}

auto ProcfsSourceAvailability::SupportsMetric(const MetricConfig& config, bool is_composite,
                                              const std::string& label) const
    -> astl::compat::expected<bool, astl_status_code> {
  if (is_composite && !ReadableCompositeInputs(*_files, config)) {
    return false;
  }
  // Optional system sources may be absent, masked, or permission restricted.
  if (!IsSystem() && _settings->relative_path != "loadavg") {
    return true;
  }
  auto field = ParseProcfsFieldDescriptor(
      *_settings, label.empty() ? astl::compat::optional<std::string>{} : astl::compat::optional<std::string>{label});
  if (!field) {
    return astl::compat::unexpected(field.error());
  }
  if (_interrupt_counters) {
    return _interrupt_counters->contains(std::get<procfs::SystemField>(*field).label);
  }
  return ReadableSystemSource(*_files, *field);
}
}  // namespace astl::procfs_metric_builder_helpers::detail
