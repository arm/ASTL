// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "astl_cxx_compat.hpp"
#include "astl_logger.hpp"
#include "procfs/procfs_collector.hpp"
#include "procfs/procfs_utils.hpp"

namespace astl {

auto ProcfsCollector::FindCpuSnapshot(const procfs::CpuUtilizationField& cpu_field,
                                      const CpuSnapshotCache&            cpu_snapshot_cache)
    -> astl::compat::expected<const procfs::CpuSnapshot*, astl_status_code> {
  const auto path_it = cpu_snapshot_cache.find(cpu_field.relative_path);
  if (path_it == cpu_snapshot_cache.end()) {
    ASTL_LOG_ERROR("ProcfsCollector: missing cached CPU snapshot file '{}'", cpu_field.relative_path.string());
    return astl::compat::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  const auto snapshot_it = path_it->second.find(cpu_field.line_prefix);
  if (snapshot_it == path_it->second.end()) {
    ASTL_LOG_ERROR("ProcfsCollector: CPU '{}' is missing from '{}'", cpu_field.line_prefix,
                   cpu_field.relative_path.string());
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return &snapshot_it->second;
}

auto ProcfsCollector::ReadCpuUtilizationSample(const ProcfsReadOperation&         operation,
                                               const procfs::CpuUtilizationField& cpu_field,
                                               const procfs::CpuSnapshot&         cpu_snapshot)
    -> astl::compat::optional<AstlValue> {
  const auto operation_id = operation.GetId();
  const auto previous_it  = _previous_cpu_snapshots.find(operation_id);
  if (previous_it == _previous_cpu_snapshots.end()) {
    _previous_cpu_snapshots.emplace(operation_id, cpu_snapshot);
    return astl::compat::nullopt;
  }

  const auto& previous_snapshot = previous_it->second;
  if (cpu_snapshot.total > previous_snapshot.total && cpu_snapshot.idle >= previous_snapshot.idle) {
    const auto total_delta = cpu_snapshot.total - previous_snapshot.total;
    const auto idle_delta  = cpu_snapshot.idle - previous_snapshot.idle;
    if (idle_delta > total_delta) {
      ASTL_LOG_WARNING(
          "ProcfsCollector: CPU '{}' idle_delta={} is greater than total_delta={}; previous_total={}, "
          "current_total={}, previous_idle={}, current_idle={}",
          cpu_field.line_prefix, idle_delta, total_delta, previous_snapshot.total, cpu_snapshot.total,
          previous_snapshot.idle, cpu_snapshot.idle);
    } else if (idle_delta == 0) {
      ASTL_LOG_DEBUG(
          "ProcfsCollector: CPU '{}' utilization=100%; total_delta={}, idle_delta={}; previous_total={}, "
          "current_total={}, previous_idle={}, current_idle={}",
          cpu_field.line_prefix, total_delta, idle_delta, previous_snapshot.total, cpu_snapshot.total,
          previous_snapshot.idle, cpu_snapshot.idle);
    }
  }

  const auto old      = previous_snapshot;
  previous_it->second = cpu_snapshot;
  if (cpu_field.iowait) {
    if (!cpu_snapshot.has_iowait || !old.has_iowait || cpu_snapshot.total <= old.total ||
        cpu_snapshot.iowait < old.iowait || cpu_snapshot.iowait - old.iowait > cpu_snapshot.total - old.total) {
      return astl::compat::nullopt;
    }
    return AstlValue{100.0 * static_cast<double>(cpu_snapshot.iowait - old.iowait) /
                     static_cast<double>(cpu_snapshot.total - old.total)};
  }
  const auto utilization = procfs::CalculateCpuUtilization(old, cpu_snapshot);
  return AstlValue{utilization};
}

auto ProcfsCollector::ReadOperationSample(const ProcfsReadOperation& operation, const PreparedOperations& prepared)
    -> astl::compat::expected<astl::compat::optional<AstlValue>, astl_status_code> {
  if (const auto* cpu_field = std::get_if<procfs::CpuUtilizationField>(&operation.field_descriptor)) {
    auto snapshot_or_error = FindCpuSnapshot(*cpu_field, prepared.cpu_snapshots);
    if (!snapshot_or_error.has_value()) {
      return astl::compat::unexpected(snapshot_or_error.error());
    }
    return ReadCpuUtilizationSample(operation, *cpu_field, **snapshot_or_error);
  }

  return ReadFieldSample(operation, prepared);
}

auto ProcfsCollector::ReadFieldSample(const ProcfsReadOperation& operation, const PreparedOperations& prepared)
    -> astl::compat::expected<astl::compat::optional<AstlValue>, astl_status_code> {
  std::string source_identity;
  bool        missing_label  = false;
  const auto* field          = std::get_if<procfs::SystemField>(&operation.field_descriptor);
  auto        value_or_error = field ? ReadSystemSample(*field, prepared, source_identity, missing_label)
                                     : procfs::ReadField(_procfs_file_interface, operation.field_descriptor);
  if (!value_or_error.has_value()) {
    if (missing_label) {
      // A known device/interface vanished. Never bridge the gap on reappearance.
      _counter_rates.erase(operation.GetId());
      return astl::compat::optional<AstlValue>{};
    }
    if (field && (field->kind == "threads" || field->kind == "processes") &&
        (value_or_error.error() == ASTL_STATUS_FILE_ERROR || value_or_error.error() == ASTL_STATUS_FILE_OPEN_FAILED ||
         value_or_error.error() == ASTL_STATUS_BAD_CONFIGURATION)) {
      // Task scans are optional, non-atomic gauges. Preserve other samples and rate baselines.
      ASTL_LOG_DEBUG("ProcfsCollector: omitting unavailable current {} sample", field->kind);
      return astl::compat::optional<AstlValue>{};
    }
    return astl::compat::unexpected(value_or_error.error());
  }
  if (field && field->rate) {
    return ReadRateSample(operation.GetId(), *field, *value_or_error, prepared, source_identity);
  }
  return astl::compat::optional<AstlValue>{std::move(*value_or_error)};
}

auto ProcfsCollector::ReadSystemSample(const procfs::SystemField& field, const PreparedOperations& prepared,
                                       std::string& source_identity, bool& missing_label)
    -> astl::compat::expected<AstlValue, astl_status_code> {
  if (field.kind != "interrupts") {
    return procfs::ReadSystemField(_procfs_file_interface, field, &source_identity, &missing_label);
  }
  const auto& counters = prepared.interrupt_snapshots.at(field.relative_path);
  const auto  it       = counters.find(field.label);
  if (it == counters.end()) {
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return AstlValue{it->second};
}

auto ProcfsCollector::ReadRateSample(OperationId operation_id, const procfs::SystemField& field, const AstlValue& value,
                                     const PreparedOperations& prepared, std::string_view source_identity)
    -> astl::compat::expected<astl::compat::optional<AstlValue>, astl_status_code> {
  const auto* counter = std::get_if<uint64_t>(&value.value);
  if (counter == nullptr) {
    return astl::compat::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  // Cached columns share the time of the table read, independent of later
  // operations (including potentially expensive process/thread scans).
  const auto sample_time =
      field.kind == "interrupts" ? prepared.interrupt_times.at(field.relative_path) : _sample_clock();
  const auto identity = field.kind == "interrupts"
                            ? std::string_view{prepared.interrupt_identities.at(field.relative_path)}
                            : std::string_view{source_identity};
  if (field.kind == "interrupts" && prepared.interrupt_resets.contains(field.relative_path)) {
    _counter_rates.erase(operation_id);
  }
  auto rate = _counter_rates[operation_id].Sample(*counter, sample_time, identity);
  if (!rate) {
    return astl::compat::optional<AstlValue>{};
  }
  return astl::compat::optional<AstlValue>{AstlValue{*rate}};
}

}  // namespace astl
