// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "astl_cxx_compat.hpp"
#include "astl_logger.hpp"
#include "procfs/procfs_collector.hpp"
#include "procfs/procfs_utils.hpp"

namespace astl {

auto ProcfsCollector::InterruptSnapshot::HasReset(const InterruptSnapshot& previous) const -> bool {
  if (previous.identity != identity || previous.components.size() != components.size()) {
    return false;
  }
  for (size_t index = 0; index < components.size(); ++index) {
    if (components[index] < previous.components[index]) {
      return true;
    }
  }
  return false;
}

auto ProcfsCollector::PrepareInterruptSnapshot(const std::filesystem::path& path, PreparedOperations& prepared)
    -> astl_status_code {
  InterruptSnapshot snapshot;
  auto counters = procfs::ReadInterruptCounters(_procfs_file_interface, path, &snapshot.identity, &snapshot.components);
  if (!counters) {
    return counters.error();
  }
  prepared.interrupt_times.emplace(path, _sample_clock());
  const auto previous = _previous_interrupt_snapshots.find(path);
  if (previous != _previous_interrupt_snapshots.end() && snapshot.HasReset(previous->second)) {
    prepared.interrupt_resets.insert(path);
  }
  prepared.interrupt_identities.emplace(path, snapshot.identity);
  _previous_interrupt_snapshots.insert_or_assign(path, std::move(snapshot));
  prepared.interrupt_snapshots.emplace(path, std::move(*counters));
  return ASTL_STATUS_SUCCESS;
}

auto ProcfsCollector::PrepareOperations(const OperationSequence& operations)
    -> astl::compat::expected<PreparedOperations, astl_status_code> {
  PreparedOperations prepared;
  prepared.operations.reserve(operations.size());

  for (const auto& operation : operations) {
    const auto* procfs_operation = dynamic_cast<const ProcfsReadOperation*>(operation.get());
    if (procfs_operation == nullptr) {
      ASTL_LOG_ERROR("ProcfsCollector: invalid operation type");
      return astl::compat::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    prepared.operations.push_back(procfs_operation);
    if (const auto* field = std::get_if<procfs::SystemField>(&procfs_operation->field_descriptor);
        field && field->kind == "interrupts" && !prepared.interrupt_snapshots.contains(field->relative_path)) {
      const auto status = PrepareInterruptSnapshot(field->relative_path, prepared);
      if (status != ASTL_STATUS_SUCCESS) {
        return astl::compat::unexpected(status);
      }
    }

    const auto* cpu_field = std::get_if<procfs::CpuUtilizationField>(&procfs_operation->field_descriptor);
    if (cpu_field == nullptr || prepared.cpu_snapshots.contains(cpu_field->relative_path)) {
      continue;
    }

    auto snapshots_or_error = procfs::ReadCpuSnapshots(_procfs_file_interface, cpu_field->relative_path);
    if (!snapshots_or_error.has_value()) {
      ASTL_LOG_ERROR("ProcfsCollector: failed to read procfs CPU snapshots");
      return astl::compat::unexpected(snapshots_or_error.error());
    }
    prepared.cpu_snapshots.emplace(cpu_field->relative_path, std::move(*snapshots_or_error));
  }
  return prepared;
}

}  // namespace astl
