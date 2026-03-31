// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/util/delimited_message_util.h>

#include <chrono>
#include <fstream>
#include <istream>
#include <ostream>

#include "astl/astl_errors.h"
#include "serdes/protobuf_serdes.hpp"
#include "serdes/raw_samples.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.
#include "serdes/serdes_util.hpp"

namespace astl::ProtobufSerDes {

namespace fs = std::filesystem;

auto Serialize(const std::vector<RawSampledData>& samples, std::ostream& output_stream) -> astl_status_code {
  using google::protobuf::util::SerializeDelimitedToOstream;

  astl::protobuf::RawSampleBatch batch;
  if (samples.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    ASTL_LOG_ERROR("Too many samples to serialize: {}", samples.size());
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  batch.mutable_samples()->Reserve(static_cast<int>(samples.size()));

  for (const auto& sample : samples) {
    auto* proto_sample = batch.add_samples();
    proto_sample->set_operation_id(sample.operation_id);
    proto_sample->set_timestamp_us(sample.raw_tick);
    astl::protobuf::AstlValue* proto_value = proto_sample->mutable_value();
    try {
      std::visit([&](const auto& val) { detail::SetOneOf(*proto_value, val); }, sample.value.value);
    } catch (const std::bad_variant_access& ex) {
      ASTL_LOG_ERROR("Failed to serialize AstlValue in RawSample: {}", ex.what());
      return ASTL_STATUS_INTERNAL_ERROR;
    }
  }

  if (!SerializeDelimitedToOstream(batch, &output_stream)) {
    ASTL_LOG_ERROR("Failed to serialize RawSampleBatch to output stream");
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  return ASTL_STATUS_SUCCESS;
}

template <>
auto Deserialize<std::vector<RawSampledData>>(std::istream& input_stream)
    -> std::expected<std::vector<RawSampledData>, astl_status_code> {
  using google::protobuf::io::IstreamInputStream;
  using google::protobuf::util::ParseDelimitedFromZeroCopyStream;

  IstreamInputStream zero_copy_input(&input_stream);

  std::vector<RawSampledData>    result;
  astl::protobuf::RawSampleBatch batch;

  bool clean_eof = false;
  while (ParseDelimitedFromZeroCopyStream(&batch, &zero_copy_input, &clean_eof)) {
    // Convert one batch
    // ensure sufficient capacity. at least geometric growth to avoid O(n^2) copies,
    // and also enough for all the new samples
    const auto num_new_samples = static_cast<size_t>(batch.samples_size());
    if (result.capacity() < result.size() + num_new_samples) {
      auto new_size = std::max(result.capacity() * 2, result.size() + num_new_samples);
      result.reserve(new_size);
    }
    for (const auto& proto_sample : batch.samples()) {
      auto value_or = detail::DeserializeAstlValue(proto_sample.value());
      if (!value_or.has_value()) {
        ASTL_LOG_ERROR("Failed to convert protobuf AstlValue in RawSample");
        return std::unexpected(value_or.error());
      }

      const uint64_t op_id64 = proto_sample.operation_id();
      if (op_id64 > std::numeric_limits<OperationId>::max()) {
        ASTL_LOG_ERROR("OperationId value out of range: {}", op_id64);
        return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
      }
      const OperationId op_id = static_cast<OperationId>(op_id64);

      RawSampledData sample{op_id, std::move(*value_or)};
      sample.raw_tick = proto_sample.timestamp_us();

      result.emplace_back(std::move(sample));
    }

    batch.Clear();
  }

  if (!clean_eof) {
    // Loop exited due to parse failure rather than clean EOF
    ASTL_LOG_ERROR("Failed to parse RawSampleBatch from input stream");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  return result;
}

auto SerializeCurrentBatch(const std::string& target_name, const std::vector<RawSampledData>& samples,
                           fs::path output_path) -> astl_status_code {
  if (samples.empty()) {
    ASTL_LOG_WARNING("No samples to serialize for target {}", target_name);
    return ASTL_STATUS_NO_DATA_COLLECTED;
  }

  fs::create_directories(output_path);
  const fs::path file_path = output_path / (target_name + kAstlFileExtension);

  std::ofstream ofs(file_path, std::ios::binary | std::ios::app);
  if (!ofs) {
    ASTL_LOG_ERROR("Failed to open {} for writing", file_path.string());
    return ASTL_STATUS_INTERNAL_ERROR;
  }

  auto result = Serialize(samples, ofs);
  if (result != ASTL_STATUS_SUCCESS) {
    ASTL_LOG_ERROR("Failed to serialize {} samples to {}", samples.size(), file_path.string());
    return result;
  }

  ASTL_LOG_DEBUG("Serialized {} samples to {}", samples.size(), file_path.string());
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl::ProtobufSerDes
