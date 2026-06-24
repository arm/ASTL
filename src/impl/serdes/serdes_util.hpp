// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_SERDES_UTIL_HPP_
#define ASTL_SERDES_UTIL_HPP_

#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "astl/astl_errors.h"
#include "astl_logger.hpp"
#include "astl_value.hpp"
#include "operation/operation.hpp"
#include "serdes/serdes_common.pb.h"

namespace astl::ProtobufSerDes::detail {

template <typename>
struct dependent_false : std::false_type {};

template <typename ProtoOneOf, typename T>
inline void SetOneOf(ProtoOneOf& out, const T& value) {
  using U = std::decay_t<T>;
  if constexpr (std::is_same_v<U, std::uint8_t>) {
    out.set_uint8_value(static_cast<std::uint32_t>(value));
  } else if constexpr (std::is_same_v<U, std::uint16_t>) {
    out.set_uint16_value(static_cast<std::uint32_t>(value));
  } else if constexpr (std::is_same_v<U, std::uint32_t>) {
    out.set_uint32_value(value);
  } else if constexpr (std::is_same_v<U, std::uint64_t>) {
    out.set_uint64_value(value);
  } else if constexpr (std::is_same_v<U, float>) {
    out.set_float_value(value);
  } else if constexpr (std::is_same_v<U, double>) {
    out.set_double_value(value);
  } else if constexpr (std::is_same_v<U, bool>) {
    out.set_bool_value(value);
  } else {
    static_assert(dependent_false<U>::value, "Unsupported AstlValue type");
  }
}

template <typename ProtoOneOf>
inline std::expected<AstlValue, astl_status_code> DeserializeAstlValue(const ProtoOneOf& msg) {
  using ProtoVal = ProtoOneOf;

  switch (msg.value_case()) {
    case ProtoVal::kUint8Value:
      return AstlValue{static_cast<std::uint8_t>(msg.uint8_value())};
    case ProtoVal::kUint16Value:
      return AstlValue{static_cast<std::uint16_t>(msg.uint16_value())};
    case ProtoVal::kUint32Value:
      return AstlValue{static_cast<std::uint32_t>(msg.uint32_value())};
    case ProtoVal::kUint64Value:
      return AstlValue{static_cast<std::uint64_t>(msg.uint64_value())};
    case ProtoVal::kFloatValue:
      return AstlValue{msg.float_value()};
    case ProtoVal::kDoubleValue:
      return AstlValue{msg.double_value()};
    case ProtoVal::kBoolValue:
      return AstlValue{msg.bool_value()};

    case ProtoVal::VALUE_NOT_SET:
    default:
      ASTL_LOG_ERROR("Unknown or unset value type in protobuf one-of");
      return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }
}

inline void SerializeAstlValue(const AstlValue& src, astl::protobuf::AstlValue& dst) {
  std::visit([&](const auto& val) { SetOneOf(dst, val); }, src.value);
}

inline auto DeserializeOperationId(std::uint64_t raw_operation_id, std::string_view context)
    -> std::expected<OperationId, astl_status_code> {
  if (raw_operation_id == kOperationIdInvalid) {
    ASTL_LOG_ERROR("{} OperationId value is reserved invalid id: {}", context, raw_operation_id);
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  if (raw_operation_id > std::numeric_limits<OperationId>::max()) {
    ASTL_LOG_ERROR("{} OperationId value out of range: {}", context, raw_operation_id);
    return std::unexpected(ASTL_STATUS_INVALID_VALUE_TYPE);
  }

  return static_cast<OperationId>(raw_operation_id);
}

}  // namespace astl::ProtobufSerDes::detail

#endif  // ASTL_SERDES_UTIL_HPP_
