// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message_lite.h>
#include <google/protobuf/util/delimited_message_util.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <iterator>
#include <limits>
#include <memory>
#include <ostream>

#include "astl/astl_errors.h"
#include "serdes/protobuf_serdes.hpp"
#include "serdes/raw_samples.pb.h"  // AUTO-GENERATED FILE. Re-render using cmake proto_gen target.
#include "serdes/serdes_util.hpp"

namespace astl::ProtobufSerDes {

namespace {
auto ShutdownProtobufLibraryAtExit() noexcept -> void { google::protobuf::ShutdownProtobufLibrary(); }

[[maybe_unused]] const bool kRegisteredProtobufShutdown = std::atexit(ShutdownProtobufLibraryAtExit) == 0;

auto ConvertRawSampleBatch(const astl::protobuf::RawSampleBatch& batch, std::vector<RawSampledData>& result)
    -> astl_status_code {
  const auto num_new_samples = static_cast<size_t>(batch.samples_size());
  result.reserve(result.size() + num_new_samples);

  for (const auto& proto_sample : batch.samples()) {
    auto value_or = detail::DeserializeAstlValue(proto_sample.value());
    if (!value_or.has_value()) {
      ASTL_LOG_ERROR("Failed to convert protobuf AstlValue in RawSample");
      return value_or.error();
    }

    auto operation_id_or_error = detail::DeserializeOperationId(proto_sample.operation_id(), "RawSample");
    if (!operation_id_or_error.has_value()) {
      return operation_id_or_error.error();
    }

    RawSampledData sample{*operation_id_or_error, std::move(*value_or)};
    sample.raw_tick = proto_sample.timestamp_us();

    result.emplace_back(std::move(sample));
  }

  return ASTL_STATUS_SUCCESS;
}
}  // namespace

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
  std::vector<RawSampledData> result;
  RawSampleBatchReader        reader{input_stream};

  while (true) {
    auto batch_or_error = reader.ReadNext();
    if (!batch_or_error) {
      return std::unexpected(batch_or_error.error());
    }
    auto& batch_samples = *batch_or_error;
    if (batch_samples.empty()) {
      break;
    }

    if (result.capacity() < result.size() + batch_samples.size()) {
      auto new_size = std::max(result.capacity() * 2, result.size() + batch_samples.size());
      result.reserve(new_size);
    }
    result.insert(result.end(), std::make_move_iterator(batch_samples.begin()),
                  std::make_move_iterator(batch_samples.end()));
  }

  return result;
}

class RawSampleBatchReader::Impl {
 public:
  explicit Impl(std::istream& input_stream) : zero_copy_input{&input_stream} {}

  google::protobuf::io::IstreamInputStream zero_copy_input;
};

RawSampleBatchReader::RawSampleBatchReader(std::istream& input_stream) : _impl{std::make_unique<Impl>(input_stream)} {}

RawSampleBatchReader::~RawSampleBatchReader() = default;

RawSampleBatchReader::RawSampleBatchReader(RawSampleBatchReader&&) noexcept = default;

auto RawSampleBatchReader::operator=(RawSampleBatchReader&&) noexcept -> RawSampleBatchReader& = default;

auto RawSampleBatchReader::ReadNext() -> std::expected<std::vector<RawSampledData>, astl_status_code> {
  using google::protobuf::util::ParseDelimitedFromZeroCopyStream;

  if (!_impl) {
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }

  astl::protobuf::RawSampleBatch batch;
  bool                           clean_eof = false;
  if (!ParseDelimitedFromZeroCopyStream(&batch, &_impl->zero_copy_input, &clean_eof)) {
    if (clean_eof) {
      return std::vector<RawSampledData>{};
    }
    ASTL_LOG_ERROR("Failed to parse RawSampleBatch from input stream");
    return std::unexpected(ASTL_STATUS_INTERNAL_ERROR);
  }

  std::vector<RawSampledData> batch_samples;
  auto                        conversion_status = ConvertRawSampleBatch(batch, batch_samples);
  if (conversion_status != ASTL_STATUS_SUCCESS) {
    return std::unexpected(conversion_status);
  }

  return batch_samples;
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
