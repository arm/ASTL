// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_SCMI_UUID_HPP_
#define ASTL_SCMI_UUID_HPP_

#include <algorithm>
#include <expected>
#include <stdexcept>
#include <string>
#include <utility>

#include "astl/astl_errors.h"
#include "astl_logger.hpp"

namespace astl::scmi::spec {

struct Uuid {
  // Contains the full UUID string, including non-significant bytes,
  // but has extra whitespace from JSON input trimmed (normalized).
  std::string normalized_value;
  size_t      num_significant_bytes;

  /** constructor should only be invoked by GetNormalizedUuid normally */
  Uuid(std::string value, size_t significant_bytes)
      : normalized_value(std::move(value)), num_significant_bytes(significant_bytes) {}

  auto SignificantPart() const -> std::string_view {
    return std::string_view{normalized_value}.substr(0, num_significant_bytes * 2);
  }
};

// UUID equality only depends on the upper most num_significant_bytes
inline auto operator==(const Uuid& uuid_a, const Uuid& uuid_b) {
  const auto uuid_a_sig_part = uuid_a.SignificantPart();
  const auto uuid_b_sig_part = uuid_b.SignificantPart();
  auto       sig_part_len    = (std::min)(uuid_a_sig_part.size(), uuid_b_sig_part.size());
  return uuid_a_sig_part.compare(0, sig_part_len, uuid_b_sig_part, 0, sig_part_len) == 0;
}

inline auto operator!=(const Uuid& uuid_a, const Uuid& uuid_b) { return !(uuid_a == uuid_b); }

/**
 * @brief normalize a UUID string to lowercase, without hyphens, without leading 0x and leading trailing spaces.
 * return as a pair of Uuid (normalized), and the number of significant bytes if specified by / postfix (else 16)
 */
inline auto GetNormalizedUuid(std::string const& input_uuid) -> std::expected<scmi::spec::Uuid, astl_status_code> {
  std::string normalizing_input = input_uuid;
  // remove leading/trailing spaces
  constexpr char const* whitespace      = " \t\n\r\f\v";
  const auto            first_non_space = normalizing_input.find_first_not_of(whitespace);
  if (first_non_space == std::string::npos) {
    ASTL_LOG_WARNING("GetNormalizedUuid: UUID {} is empty or all whitespace", input_uuid);
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  normalizing_input.erase(0, first_non_space);
  const auto last_non_space = normalizing_input.find_last_not_of(whitespace);
  if (last_non_space == std::string::npos) {
    ASTL_LOG_WARNING("GetNormalizedUuid: UUID {} is empty or all whitespace", input_uuid);
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  normalizing_input.erase(last_non_space + 1);

  // remove leading 0x if present
  if (normalizing_input.rfind("0x", 0) == 0 || normalizing_input.rfind("0X", 0) == 0) {
    normalizing_input = normalizing_input.substr(2);
  }
  // remove hyphens
  normalizing_input.erase(std::remove(normalizing_input.begin(), normalizing_input.end(), '-'),
                          normalizing_input.end());
  // convert to lowercase
  std::transform(normalizing_input.begin(), normalizing_input.end(), normalizing_input.begin(), ::tolower);
  // split by '/' to get significant bytes if specified
  size_t num_significant_bytes = normalizing_input.size() / 2;  // default - 1 byte per 2 hex chars
  size_t slash_pos             = normalizing_input.find('/');
  if (slash_pos != std::string::npos) {
    std::string sig_bytes_str = normalizing_input.substr(slash_pos + 1);
    try {
      num_significant_bytes = static_cast<size_t>(std::stoul(sig_bytes_str, nullptr, 0));
    } catch (const std::invalid_argument&) {
      ASTL_LOG_WARNING("GetNormalizedUuid: UUID {} has invalid significant bytes specifier '{}'", input_uuid,
                       sig_bytes_str);
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    } catch (const std::out_of_range&) {
      ASTL_LOG_WARNING("GetNormalizedUuid: UUID {} has out of range significant bytes specifier '{}'", input_uuid,
                       sig_bytes_str);
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    normalizing_input = normalizing_input.substr(0, slash_pos);
  }
  if (normalizing_input.size() < num_significant_bytes * 2) {
    ASTL_LOG_WARNING("GetNormalizedUuid: UUID {} is {} characters long, less than the {} significant bytes required",
                     normalizing_input, normalizing_input.size(), num_significant_bytes * 2);
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  if (num_significant_bytes == 0) {
    ASTL_LOG_WARNING("GetNormalizedUuid: UUID {} is specified with 0 significant bytes {}", input_uuid,
                     num_significant_bytes);
    return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
  }
  return scmi::spec::Uuid{normalizing_input, num_significant_bytes};
};

}  // namespace astl::scmi::spec

#endif  // ASTL_SCMI_UUID_HPP_
