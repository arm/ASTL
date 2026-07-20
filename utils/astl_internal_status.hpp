// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_INTERNAL_STATUS_HPP_
#define ASTL_INTERNAL_STATUS_HPP_

#include <string>
#include <string_view>

#include "astl/astl_errors.h"

namespace astl {

enum class InternalStatusCode : int {
  UNKNOWN_ERROR          = -1,
  NOT_IMPLEMENTED        = -2,
  DIVIDE_BY_ZERO         = -3,
  OPERATION_ID_EXHAUSTED = -4,
};

constexpr auto ToStatusCode(InternalStatusCode code) noexcept -> astl_status_code {
  return static_cast<astl_status_code>(static_cast<int>(code));
}

constexpr astl_status_code kInternalUnknownError         = ToStatusCode(InternalStatusCode::UNKNOWN_ERROR);
constexpr astl_status_code kInternalNotImplemented       = ToStatusCode(InternalStatusCode::NOT_IMPLEMENTED);
constexpr astl_status_code kInternalDivideByZero         = ToStatusCode(InternalStatusCode::DIVIDE_BY_ZERO);
constexpr astl_status_code kInternalOperationIdExhausted = ToStatusCode(InternalStatusCode::OPERATION_ID_EXHAUSTED);

static_assert(static_cast<int>(InternalStatusCode::UNKNOWN_ERROR) < 0);
static_assert(static_cast<int>(InternalStatusCode::NOT_IMPLEMENTED) < 0);
static_assert(static_cast<int>(InternalStatusCode::DIVIDE_BY_ZERO) < 0);
static_assert(static_cast<int>(InternalStatusCode::OPERATION_ID_EXHAUSTED) < 0);

constexpr auto IsInternalStatus(astl_status_code status) noexcept -> bool {
  switch (static_cast<int>(status)) {
    case static_cast<int>(InternalStatusCode::UNKNOWN_ERROR):
    case static_cast<int>(InternalStatusCode::NOT_IMPLEMENTED):
    case static_cast<int>(InternalStatusCode::DIVIDE_BY_ZERO):
    case static_cast<int>(InternalStatusCode::OPERATION_ID_EXHAUSTED):
      return true;
    default:
      return false;
  }
}

constexpr auto IsPublicApiStatus(astl_status_code status) noexcept -> bool {
  const int value = static_cast<int>(status);
  return (value >= 0 && value < static_cast<int>(ASTL_STATUS_INTERNAL_ERROR) && !IsInternalStatus(status)) ||
         value == static_cast<int>(ASTL_STATUS_INTERNAL_ERROR);
}

constexpr auto InternalStatusString(astl_status_code status) noexcept -> const char* {
  switch (static_cast<int>(status)) {
    case static_cast<int>(InternalStatusCode::UNKNOWN_ERROR):
      return "UNKNOWN_ERROR";
    case static_cast<int>(InternalStatusCode::NOT_IMPLEMENTED):
      return "NOT_IMPLEMENTED";
    case static_cast<int>(InternalStatusCode::DIVIDE_BY_ZERO):
      return "DIVIDE_BY_ZERO";
    case static_cast<int>(InternalStatusCode::OPERATION_ID_EXHAUSTED):
      return "OPERATION_ID_EXHAUSTED";
    default:
      return "UNKNOWN_ERROR";
  }
}

auto SetLastStatusString(std::string message) noexcept -> void;
auto SetLastStatusString(std::string_view message) noexcept -> void;
auto SetLastStatusFallback(astl_status_code status) noexcept -> void;
auto MaybeSetLastStatusFallback(astl_status_code status) noexcept -> void;
auto ClearLastStatusString() noexcept -> void;
auto GetLastStatusStringView() noexcept -> const char*;
auto CaptureLoggedStatusMessage(std::string_view message) noexcept -> void;

}  // namespace astl

#endif  // ASTL_INTERNAL_STATUS_HPP_
