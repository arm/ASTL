// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file protobuf_serdes.hpp
 * @brief Defines a generic serialization/deserialization interface for ASTL objects using Protocol Buffers.
 *
 * This module provides helper functions to serialize and deserialize various ASTL
 * data structures—currently supporting `RawSampledData`, with planned extensions
 * for additional object types (e.g., configuration or metrics).
 *
 * The functions encode in-memory C++ objects into protobuf binary format for storage
 * or transmission, and decode them back to native ASTL types.
 *
 * Usage:
 * - Call `Serialize()` and `Deserialize()` for stream-based conversions.
 * - Use `SerializeCurrentBatch()` for persistent storage of sample batches.
 * - When adding support for new object types:
 *   1. Define corresponding protobuf messages in the ASTL schema.
 *   2. Implement overloaded `Serialize()` and `Deserialize()` functions.
 */
#ifndef ASTL_PROTOBUF_SERDES_HPP_
#define ASTL_PROTOBUF_SERDES_HPP_

#include <expected>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <type_traits>
#include <vector>

#include "i_raw_sample_sink.hpp"
#include "topology/i_topology_manager.hpp"

namespace astl {

constexpr const char* kAstlFileExtension       = ".astl";
constexpr const char* kTopologyManagerFileName = "topology_manager.astl";
constexpr const char* kMetricManagerFileName   = "metric_manager.astl";

struct MetricHandle;
struct IMetricManager;
class MetricManager;

namespace ProtobufSerDes {

/**
 * @brief Serializes a collection of RawSampledData objects into a binary stream.
 *
 * @param[in] samples Collection of samples to serialize.
 * @param[out] output_stream Output stream to write serialized data.
 * @return astl_status_code ASTL_STATUS_SUCCESS on success, error code otherwise.
 */
auto Serialize(const std::vector<RawSampledData>& samples, std::ostream& output_stream) -> astl_status_code;

/**
 * @brief Reads raw samples one serialized protobuf batch at a time from a stream.
 *
 * Each ReadNext() call parses at most one RawSampleBatch. An empty vector means clean EOF or an empty batch.
 */
class RawSampleBatchReader {
 public:
  explicit RawSampleBatchReader(std::istream& input_stream);
  ~RawSampleBatchReader();

  RawSampleBatchReader(RawSampleBatchReader&&) noexcept;
  auto operator=(RawSampleBatchReader&&) noexcept -> RawSampleBatchReader&;

  RawSampleBatchReader(const RawSampleBatchReader&)                    = delete;
  auto operator=(const RawSampleBatchReader&) -> RawSampleBatchReader& = delete;

  auto ReadNext() -> std::expected<std::vector<RawSampledData>, astl_status_code>;

 private:
  class Impl;
  std::unique_ptr<Impl> _impl;
};

/**
 * @brief Deserializes protobuf-encoded data into a supported type `T`.
 *
 * Converts a protobuf binary stream into a C++ object of type `T`. Supported
 * types include any `T` that satisfies the `Deserializable` concept, which
 * requires:
 *   - `T` defines a `static constexpr bool kSerializable` set to true, or
 *   - `T` is a `std::vector<U>` where `U` satisfies the above.
 *
 * This includes both single payloads (e.g., `RawSampledData`) and collections
 * (`std::vector<RawSampledData>`). Unsupported types are rejected at compile time.
 *
 * @tparam T The output type. Must satisfy `Deserializable`.
 * @param[in] input_stream Stream containing protobuf-encoded data.
 * @return std::expected<T, astl_status_code>
 *         Returns the decoded payload on success, or an error code on failure.
 */
namespace detail {
template <typename T, template <typename...> class Template>
struct IsSpecializationOf : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct IsSpecializationOf<Template<Args...>, Template> : std::true_type {};
}  // namespace detail

template <typename T>
concept StdVector = detail::IsSpecializationOf<T, std::vector>::value;

template <typename T>
concept StdUniquePtr = detail::IsSpecializationOf<T, std::unique_ptr>::value;

template <typename T>
concept DeserializableBase = requires {
  { T::kSerializable } -> std::convertible_to<bool>;
} && static_cast<bool>(T::kSerializable);

template <typename T>
concept Deserializable =
    DeserializableBase<T> || (StdVector<T> && DeserializableBase<typename T::value_type>) ||
    (StdUniquePtr<T> && DeserializableBase<typename T::element_type>) ||
    (StdVector<T> && StdUniquePtr<typename T::value_type> && DeserializableBase<typename T::value_type::element_type>);

template <Deserializable T>
auto Deserialize(std::istream& input_stream) -> std::expected<T, astl_status_code>;

template <Deserializable T>
auto Deserialize(std::istream&, const std::vector<std::unique_ptr<ITarget>>& targets)
    -> std::expected<T, astl_status_code>;
/**
 * @brief Serializes the current batch of samples to a file on disk.
 *
 * @param[in] target_name Base name of the output file (without extension).
 * @param[in] samples Samples to serialize.
 * @return astl_status_code ASTL_STATUS_SUCCESS on success, or appropriate error code.
 *
 * @details
 * - Writes to tmp/<target_name>.astl.
 * - Appends to the file if it exists.
 * - Logs errors if serialization or file I/O fails.
 */
auto SerializeCurrentBatch(const std::string& target_name, const std::vector<RawSampledData>& samples,
                           std::filesystem::path output_path) -> astl_status_code;

/**
 * @brief Serializes a topology manager and its targets to a file on disk.
 *
 * Converts all targets managed by the given topology manager into their
 * protobuf representation (`astl::protobuf::Target`), stores them in a
 * `astl::protobuf::TargetList`, and writes the serialized data to the
 * specified output stream.
 *
 * Each target's name, description, collector type, and ID are copied from
 * the internal `ITarget` representation. Parent handles are currently unused.
 *
 * @param[in] topology_manager The topology manager containing the targets to serialize.
 * @param[out] output_stream Output stream that receives the serialized binary data.
 * @return astl_status_code
 *         - `ASTL_STATUS_SUCCESS` on success.
 *         - `ASTL_STATUS_INTERNAL_ERROR` if serialization or stream write fails.
 *         - Any status returned by `Serialize(const ITarget&, astl::protobuf::Target*)`
 *           when serializing individual targets.
 *
 * @see Serialize(const ITarget&, astl::protobuf::Target*)
 */
auto Serialize(const ITopologyManager& topology_manager, std::ostream& output_stream) -> astl_status_code;

/**
 * @brief Serializes a metric handle the given output stream.
 *
 * Converts all metric handles given by the metric manager into their
 * protobuf representation (`astl::protobuf::RawMetric`), stores them in a
 * `astl::protobuf::RawMetricVec`, and writes the serialized data to the
 * specified output stream.
 *
 * Each metric’s configurations are copied along with the metric's target id to rebuild.
 *
 * @param[in] MetricHandle A single metric handle to serialize.
 * @param[out] output_stream Output stream that receives the serialized binary data.
 * @return astl_status_code
 *         - ASTL_STATUS_SUCCESS on success.
 *         - ASTL_STATUS_INTERNAL_ERROR if serialization or stream write fails.
 *         - ASTL_STATUS_INVALID_VALUE_TYPE if invalid fields are encountered.
 */
auto Serialize(const MetricHandle& handle, std::ostream& output_stream) -> astl_status_code;

/*
 * @brief Serializes a metric manager and its metric handles to an output stream.
 */
auto Serialize(const IMetricManager& i_metric_manager, std::ostream& output_stream) -> astl_status_code;

/**
 * @brief Wrapper to serialize the MetricManager object
 */
auto Serialize(const MetricManager& metric_manager, std::ostream& output_stream) -> astl_status_code;
}  // namespace ProtobufSerDes

}  // namespace astl

#endif  // ASTL_PROTOBUF_SERDES_HPP_
