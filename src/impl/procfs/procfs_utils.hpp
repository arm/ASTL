// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef PROCFS_UTILS_HPP_
#define PROCFS_UTILS_HPP_

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "astl_cxx_compat.hpp"
#include "astl_file_interface.hpp"
#include "common/astl_value.hpp"

namespace astl::procfs {

inline const std::filesystem::path kDefaultProcfsRootPath{"/proc"};

auto SplitWhitespace(std::string_view line) -> std::vector<std::string>;
auto Trim(std::string_view text) -> std::string_view;

struct KeyValueField {
  std::filesystem::path relative_path;
  std::string           field_name;
  astl_value_type_t     raw_value_type{ASTL_VALUE_UINT64};
};

struct TokenField {
  std::filesystem::path relative_path;
  std::string           line_prefix;  // empty means "first non-empty line"
  size_t                token_index{0};
  astl_value_type_t     raw_value_type{ASTL_VALUE_UINT64};
};

enum class SplitTokenPart { BEFORE_DELIMITER, AFTER_DELIMITER };

struct SplitTokenField {
  std::filesystem::path relative_path;
  std::string           line_prefix;  // empty means "first non-empty line"
  size_t                token_index{0};
  char                  delimiter{'/'};
  SplitTokenPart        part{SplitTokenPart::BEFORE_DELIMITER};
  astl_value_type_t     raw_value_type{ASTL_VALUE_UINT64};
};

struct TokenSumField {
  std::filesystem::path relative_path;
  std::string           line_prefix;  // empty means "first non-empty line"
  size_t                token_start_index{0};
  size_t                token_end_index{0};
};

// Procfs counters are monotonic only within one source lifetime. A decrease re-baselines.
struct CounterRateState {
  using Clock = std::chrono::steady_clock;
  astl::compat::optional<std::pair<uint64_t, Clock::time_point>> previous;
  std::string                                                    source_identity;

  auto Sample(uint64_t value, Clock::time_point now, std::string_view identity = {}) -> astl::compat::optional<double> {
    const auto baseline    = previous;
    const bool same_source = source_identity == identity;
    source_identity        = identity;
    previous               = std::pair{value, now};
    if (!baseline || !same_source || now <= baseline->second || value < baseline->first) {
      return astl::compat::nullopt;
    }
    return static_cast<double>(value - baseline->first) / std::chrono::duration<double>(now - baseline->second).count();
  }
};

struct SystemField {
  std::filesystem::path relative_path;
  std::string           label;
  std::string           kind;
  bool                  rate{false};
};

struct CpuUtilizationField {
  std::filesystem::path relative_path;
  std::string           line_prefix;
  bool                  iowait{false};
};

struct MemUsedField {
  std::filesystem::path relative_path;
};

struct MemUsedPercentField {
  std::filesystem::path relative_path;
};

struct CpuSnapshot {
  uint64_t total{0};
  uint64_t idle{0};
  uint64_t iowait{0};
  bool     has_iowait{false};
};

using CpuSnapshotMap = std::unordered_map<std::string, CpuSnapshot>;

using FieldDescriptor = std::variant<KeyValueField, TokenField, SplitTokenField, TokenSumField, CpuUtilizationField,
                                     MemUsedField, MemUsedPercentField, SystemField>;

struct MetricDescriptor {
  std::string              metric_name;
  std::string              metric_id_suffix;
  std::string              description;
  astl_units_t             units{ASTL_UNITS_NONE};
  astl_value_type_t        value_type{ASTL_VALUE_UINT64};
  astl_value_type_t        input_value_type{ASTL_VALUE_UINT64};
  astl_metric_identifier_t identifier{ASTL_METRIC_IDENTIFIER_UNKNOWN};
  uint64_t                 scale_numerator{1};
  uint64_t                 scale_denominator{1};
  FieldDescriptor          field_descriptor;
};

using InterruptCounterMap = std::unordered_map<std::string, uint64_t>;

auto ReadInterruptCounters(const FileInterface& files, const std::filesystem::path& path,
                           std::string* source_identity = nullptr, std::vector<uint64_t>* component_counts = nullptr)
    -> astl::compat::expected<InterruptCounterMap, astl_status_code>;

// missing_label distinguishes an absent disk/interface row from malformed data or file-read errors.
auto ReadSystemField(const FileInterface& files, const SystemField& field, std::string* source_identity = nullptr,
                     bool* missing_label = nullptr) -> astl::compat::expected<AstlValue, astl_status_code>;

auto ReadField(const FileInterface& file_interface, const FieldDescriptor& field_descriptor)
    -> astl::compat::expected<AstlValue, astl_status_code>;

auto ReadCpuSnapshot(const FileInterface& file_interface, const CpuUtilizationField& field_descriptor)
    -> astl::compat::expected<CpuSnapshot, astl_status_code>;

auto ReadCpuSnapshots(const FileInterface& file_interface, const std::filesystem::path& relative_path)
    -> astl::compat::expected<CpuSnapshotMap, astl_status_code>;

auto CalculateCpuUtilization(const CpuSnapshot& previous, const CpuSnapshot& current) -> double;

auto DiscoverSystemCounterDescriptors(const FileInterface& files) -> std::vector<MetricDescriptor>;

auto DiscoverCounterDescriptors(const FileInterface& file_interface)
    -> astl::compat::expected<std::vector<MetricDescriptor>, astl_status_code>;

}  // namespace astl::procfs

#endif  // PROCFS_UTILS_HPP_
