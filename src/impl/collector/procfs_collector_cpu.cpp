// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "astl_logger.hpp"
#include "collector/procfs_collector.hpp"
#include "common/procfs_utils.hpp"

namespace astl {

auto ProcfsCollector::PrepareOperations(const OperationSequence& operations)
    -> std::expected<PreparedOperations, astl_status_code> {
  PreparedOperations prepared;
  prepared.operations.reserve(operations.size());

  for (const auto& operation : operations) {
    const auto* procfs_operation = dynamic_cast<const ProcfsReadOperation*>(operation.get());
    if (procfs_operation == nullptr) {
      ASTL_LOG_ERROR("ProcfsCollector: invalid operation type");
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    prepared.operations.push_back(procfs_operation);

    const auto* cpu_field = std::get_if<procfs::CpuUtilizationField>(&procfs_operation->field_descriptor);
    if (cpu_field == nullptr || prepared.cpu_snapshots.contains(cpu_field->relative_path)) {
      continue;
    }

    auto snapshots_or_error = procfs::ReadCpuSnapshots(_procfs_file_interface, cpu_field->relative_path);
    if (!snapshots_or_error.has_value()) {
      ASTL_LOG_ERROR("ProcfsCollector: failed to read procfs CPU snapshots");
      return std::unexpected(snapshots_or_error.error());
    }
    prepared.cpu_snapshots.emplace(cpu_field->relative_path, std::move(*snapshots_or_error));
  }
  return prepared;
}

auto ProcfsCollector::FindCpuSnapshot(const procfs::CpuUtilizationField& cpu_field,
                                      const CpuSnapshotCache&            cpu_snapshot_cache)
    -> std::expected<const procfs::CpuSnapshot*, astl_status_code> {
  const auto path_it = cpu_snapshot_cache.find(cpu_field.relative_path);
  if (path_it == cpu_snapshot_cache.end()) {
    ASTL_LOG_ERROR("ProcfsCollector: missing cached CPU snapshot file '{}'", cpu_field.relative_path.string());
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  const auto snapshot_it = path_it->second.find(cpu_field.line_prefix);
  if (snapshot_it == path_it->second.end()) {
    ASTL_LOG_ERROR("ProcfsCollector: CPU '{}' is missing from '{}'", cpu_field.line_prefix,
                   cpu_field.relative_path.string());
    return std::unexpected(ASTL_STATUS_BAD_CONFIGURATION);
  }
  return &snapshot_it->second;
}

auto ProcfsCollector::ReadCpuUtilizationSample(const ProcfsReadOperation&         operation,
                                               const procfs::CpuUtilizationField& cpu_field,
                                               const procfs::CpuSnapshot& cpu_snapshot) -> std::optional<AstlValue> {
  const auto operation_id = operation.GetId();
  const auto previous_it  = _previous_cpu_snapshots.find(operation_id);
  if (previous_it == _previous_cpu_snapshots.end()) {
    _previous_cpu_snapshots.emplace(operation_id, cpu_snapshot);
    return std::nullopt;
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

  const auto utilization = procfs::CalculateCpuUtilization(previous_snapshot, cpu_snapshot);
  previous_it->second    = cpu_snapshot;
  return AstlValue{utilization};
}

auto ProcfsCollector::ReadOperationSample(const ProcfsReadOperation& operation,
                                          const CpuSnapshotCache&    cpu_snapshot_cache)
    -> std::expected<std::optional<AstlValue>, astl_status_code> {
  if (const auto* cpu_field = std::get_if<procfs::CpuUtilizationField>(&operation.field_descriptor)) {
    auto snapshot_or_error = FindCpuSnapshot(*cpu_field, cpu_snapshot_cache);
    if (!snapshot_or_error.has_value()) {
      return std::unexpected(snapshot_or_error.error());
    }
    return ReadCpuUtilizationSample(operation, *cpu_field, **snapshot_or_error);
  }

  auto value_or_error = procfs::ReadField(_procfs_file_interface, operation.field_descriptor);
  if (!value_or_error.has_value()) {
    return std::unexpected(value_or_error.error());
  }
  return std::optional<AstlValue>{std::move(*value_or_error)};
}

}  // namespace astl
