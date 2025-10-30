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
#include <vector>

#include "i_raw_sample_sink.hpp"

namespace astl::ProtobufSerDes {

/**
 * @brief Serializes a collection of RawSampledData objects into a binary stream.
 *
 * @param[in] samples Collection of samples to serialize.
 * @param[out] output_stream Output stream to write serialized data.
 * @return astl_status_code ASTL_STATUS_SUCCESS on success, error code otherwise.
 */
auto Serialize(const std::vector<RawSampledData>& samples, std::ostream& output_stream) -> astl_status_code;

/**
 * @brief Deserializes a binary stream into a vector of RawSampledData objects.
 *
 * @param[in] input_stream Input stream containing serialized protobuf data.
 * @return std::expected<std::vector<RawSampledData>, astl_status_code> Deserialized samples or error code.
 */
auto Deserialize(std::istream& input_stream) -> std::expected<std::vector<RawSampledData>, astl_status_code>;

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
 *
 * @TODO(ASTL-233) - Replace per-target output with a single session-scoped .astl cache file
 */
auto SerializeCurrentBatch(const std::string& target_name, const std::vector<RawSampledData>& samples)
    -> astl_status_code;

}  // namespace astl::ProtobufSerDes

#endif  // ASTL_PROTOBUF_SERDES_HPP_
