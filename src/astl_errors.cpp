// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <string_view>

#include "astl/astl.h"
#include "astl_internal_status.hpp"
#include "astl_magic_enum.hpp"

auto astlStatusString(astl_status_code status) noexcept -> const char* {
  if (astl::IsInternalStatus(status)) {
    return astl::InternalStatusString(status);
  }

  std::string_view name = magic_enum::enum_name(status);
  // ignore the first part of the name, which is "ASTL_STATUS_";
  constexpr size_t prefix_length = 12;  // length of "ASTL_STATUS_";
  return name.empty() ? "UNKNOWN_ERROR" : name.data() + prefix_length;
}

namespace astl {

namespace {

auto ThreadLocalLastStatusString() noexcept -> std::string& {
  thread_local std::string thread_local_last_status_string;
  return thread_local_last_status_string;
}

auto StatusDetailString(astl_status_code status) noexcept -> const char* {
  return IsInternalStatus(status) ? InternalStatusString(status) : astlStatusString(status);
}

}  // namespace

auto SetLastStatusString(std::string message) noexcept -> void { ThreadLocalLastStatusString() = std::move(message); }

auto SetLastStatusString(std::string_view message) noexcept -> void { ThreadLocalLastStatusString().assign(message); }

auto SetLastStatusFallback(astl_status_code status) noexcept -> void {
  SetLastStatusString(std::string_view{StatusDetailString(status)});
}

auto MaybeSetLastStatusFallback(astl_status_code status) noexcept -> void {
  if (ThreadLocalLastStatusString().empty()) {
    SetLastStatusFallback(status);
  }
}

auto ClearLastStatusString() noexcept -> void { ThreadLocalLastStatusString().clear(); }

auto GetLastStatusStringView() noexcept -> const char* { return ThreadLocalLastStatusString().c_str(); }

auto CaptureLoggedStatusMessage(std::string_view message) noexcept -> void { SetLastStatusString(message); }

}  // namespace astl

auto astlGetLastStatusString(void) noexcept -> const char* { return astl::GetLastStatusStringView(); }
