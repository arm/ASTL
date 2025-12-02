/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include "serdes/protobuf_serdes.hpp"

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/util/delimited_message_util.h>

#include <chrono>
#include <fstream>
#include <istream>
#include <ostream>

#include "astl/astl_errors.h"
#include "serdes/raw_samples.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.

namespace astl::ProtobufSerDes {

namespace fs = std::filesystem;

namespace detail {

template <typename T>
inline void SetOneOf(astl::protobuf::RawSample& out, const T& value) {
  using U = std::decay_t<T>;
  if constexpr (std::is_same_v<U, uint8_t>) {
    out.set_uint8_value(static_cast<uint32_t>(value));
  } else if constexpr (std::is_same_v<U, uint16_t>) {
    out.set_uint16_value(static_cast<uint32_t>(value));
  } else if constexpr (std::is_same_v<U, uint32_t>) {
    out.set_uint32_value(value);
  } else if constexpr (std::is_same_v<U, uint64_t>) {
    out.set_uint64_value(value);
  } else if constexpr (std::is_same_v<U, float>) {
    out.set_float_value(value);
  } else if constexpr (std::is_same_v<U, double>) {
    out.set_double_value(value);
  } else if constexpr (std::is_same_v<U, bool>) {
    out.set_bool_value(value);
  } else if constexpr (std::is_same_v<U, std::string>) {
    out.set_string_value(value);
  } else {
    static_assert(false, "Unsupported AstlValue type");
  }
}

static inline std::expected<AstlValue, astl_status_code> ToAstlValue(const astl::protobuf::RawSample& msg) {
  switch (msg.value_case()) {
    case astl::protobuf::RawSample::kUint8Value: {
      return AstlValue{static_cast<uint8_t>(msg.uint8_value())};
    }
    case astl::protobuf::RawSample::kUint16Value: {
      return AstlValue{static_cast<uint16_t>(msg.uint16_value())};
    }
    case astl::protobuf::RawSample::kUint32Value: {
      return AstlValue{static_cast<uint32_t>(msg.uint32_value())};
    }
    case astl::protobuf::RawSample::kUint64Value: {
      return AstlValue{msg.uint64_value()};
    }
    case astl::protobuf::RawSample::kFloatValue:
      return AstlValue{msg.float_value()};

    case astl::protobuf::RawSample::kDoubleValue:
      return AstlValue{msg.double_value()};

    case astl::protobuf::RawSample::kBoolValue:
      return AstlValue{msg.bool_value()};

    case astl::protobuf::RawSample::kStringValue:
      return AstlValue{msg.string_value()};

    case astl::protobuf::RawSample::VALUE_NOT_SET:
    default:
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

}  // namespace detail

auto Serialize(const std::vector<RawSampledData>& samples, std::ostream& output_stream) -> astl_status_code {
  using google::protobuf::util::SerializeDelimitedToOstream;

  astl::protobuf::RawSampleBatch batch;
  if (samples.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return ASTL_STATUS_INTERNAL_ERROR;
  }
  batch.mutable_samples()->Reserve(static_cast<int>(samples.size()));

  for (const auto& sample : samples) {
    auto* proto_sample = batch.add_samples();
    proto_sample->set_operation_id(sample.operation_id);
    proto_sample->set_timestamp_us(sample.timestamp.time_since_epoch().count());
    std::visit([&](const auto& val) { detail::SetOneOf(*proto_sample, val); }, sample.value.value);
  }

  if (!SerializeDelimitedToOstream(batch, &output_stream)) {
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
    result.reserve(result.size() + static_cast<size_t>(batch.samples_size()));
    for (const auto& proto_sample : batch.samples()) {
      auto value_or = detail::ToAstlValue(proto_sample);
      if (!value_or.has_value()) {
        return std::unexpected(value_or.error());
      }

      const uint64_t op_id64 = proto_sample.operation_id();
      if (op_id64 > std::numeric_limits<OperationId>::max()) {
        return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
      }
      const OperationId op_id = static_cast<OperationId>(op_id64);

      RawSampledData sample{op_id, std::move(*value_or)};
      const auto     micros = std::chrono::microseconds{proto_sample.timestamp_us()};
      sample.timestamp      = SampleTimestamp{std::chrono::duration_cast<SampleTimestamp::duration>(micros)};

      result.emplace_back(std::move(sample));
    }

    batch.Clear();
  }

  if (!clean_eof) {
    // Loop exited due to parse failure rather than clean EOF
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  return result;
}

auto SerializeCurrentBatch(const std::string& target_name, const std::vector<RawSampledData>& samples)
    -> astl_status_code {
  if (samples.empty()) {
    return ASTL_STATUS_NO_DATA_COLLECTED;
  }

  const fs::path tmp_dir = "tmp";
  fs::create_directories(tmp_dir);
  const fs::path file_path = tmp_dir / (target_name + ".astl");

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
