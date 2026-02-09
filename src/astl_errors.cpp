#include <magic_enum/magic_enum.hpp>
#include <string_view>

#include "astl/astl.h"

auto astlStatusString(astl_status_code status) noexcept -> const char* {
  std::string_view name = magic_enum::enum_name(status);
  // ignore the first part of the name, which is "ASTL_STATUS_";
  constexpr size_t prefix_length = 12;  // length of "ASTL_STATUS_";
  return name.empty() ? "UNKNOWN_ERROR" : name.data() + prefix_length;
}
