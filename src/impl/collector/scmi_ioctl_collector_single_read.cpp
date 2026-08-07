// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <expected>
#include <unordered_map>
#include <vector>

#include "astl_logger.hpp"
#include "collector/scmi_ioctl_collector.hpp"
#include "operation/scmi_read_operation.hpp"

namespace astl {

auto ScmiIoctlCollector::ReadSingleSamples() -> std::expected<std::vector<scmi_tlm_de_sample>, astl_status_code> {
  const auto data_event_count = _scmi_ioctl_interface->DataEventCount();
  if (!data_event_count) {
    return std::unexpected{data_event_count.error()};
  }

  std::vector<scmi_tlm_de_sample> samples(*data_event_count);
  uint32_t                        sample_count{};
  const auto                      read_status = _scmi_ioctl_interface->ReadSingle(samples, sample_count);
  if (read_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected{read_status};
  }
  samples.resize(sample_count);
  return samples;
}

auto ScmiIoctlCollector::EmitSingleReadSamples(OperationSequence const&               operations,
                                               std::vector<scmi_tlm_de_sample> const& samples) -> astl_status_code {
  std::unordered_map<ScmiDataEventId, scmi_tlm_de_sample> samples_by_id;
  samples_by_id.reserve(samples.size());
  for (const auto& sample : samples) {
    samples_by_id.insert_or_assign(sample.id, sample);
  }

  for (const auto& operation_ptr : operations) {
    const auto* operation = dynamic_cast<const ScmiReadOperation*>(operation_ptr.get());
    if (operation == nullptr) {
      return ASTL_STATUS_BAD_ARGUMENT;
    }
    const auto sample = samples_by_id.find(operation->scmi_data_event_id);
    if (sample == samples_by_id.end()) {
      ASTL_LOG_ERROR("SCMI_TLM_SINGLE_READ omitted enabled data event ID {:04X}", operation->scmi_data_event_id);
      return ASTL_STATUS_NO_DATA_COLLECTED;
    }
    const auto emit_status = EmitScmiSample(*operation, sample->second);
    if (emit_status != ASTL_STATUS_SUCCESS) {
      return emit_status;
    }
  }
  return ASTL_STATUS_SUCCESS;
}

auto ScmiIoctlCollector::ExecuteSingleReadOperations(OperationSequence const& operations) -> astl_status_code {
  const auto samples = ReadSingleSamples();
  if (!samples) {
    return samples.error();
  }
  return EmitSingleReadSamples(operations, *samples);
}

}  // namespace astl
