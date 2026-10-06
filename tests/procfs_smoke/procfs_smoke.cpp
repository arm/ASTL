// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "astl/astl_telemetry.h"

namespace {

constexpr uint32_t kReadCount    = 4;
constexpr auto     kReadInterval = std::chrono::milliseconds{250};

void Check(astl_status_code status, std::string_view operation) {
  if (status != ASTL_STATUS_SUCCESS) {
    throw std::runtime_error{std::string{operation} + " failed: " + std::to_string(status)};
  }
}

void Require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error{std::string{message}};
  }
}

auto GetProcfsTarget() -> astl_target_handle_t {
  // The checked count API initializes this output; cppcheck cannot track nested output pointers.
  uint32_t count;  // NOLINT(cppcoreguidelines-init-variables)
  ASTL_INIT_STRUCT(astl_get_target_count_params_t, count_params, .flags = 0, .target_count = &count);
  Check(astlGetTargetCount(&count_params), "astlGetTargetCount");
  // cppcheck cannot follow output pointers nested in const API parameter structs.
  // cppcheck-suppress uninitvar
  Require(count > 0, "No telemetry targets discovered");
  std::vector<astl_target_props_t> targets(count);
  targets.front().size = sizeof(astl_target_props_t);
  ASTL_INIT_STRUCT(astl_get_targets_params_t, params, .flags = 0, .targets = targets.data(), .target_count = &count);
  Check(astlGetTargets(&params), "astlGetTargets");
  for (const auto& target : targets) {
    std::cout << "target=" << target.name << '\n';
    if (std::string_view{target.name} == "procfs") {
      return target.handle;
    }
  }
  throw std::runtime_error{"procfs target missing"};
}

auto GetMetrics(astl_target_handle_t target) -> std::vector<astl_metric_props_t> {
  // The checked count API initializes this output; cppcheck cannot track nested output pointers.
  uint32_t count;  // NOLINT(cppcoreguidelines-init-variables)
  ASTL_INIT_STRUCT(astl_get_metric_count_params_t, count_params, .flags = 0, .target_handle = target,
                   .metric_count = &count);
  Check(astlGetMetricCountOnTarget(&count_params), "astlGetMetricCountOnTarget");
  // cppcheck cannot follow output pointers nested in const API parameter structs.
  // cppcheck-suppress uninitvar
  Require(count > 0, "No procfs metrics discovered");
  std::vector<astl_metric_props_t> metrics(count);
  metrics.front().size = sizeof(astl_metric_props_t);
  ASTL_INIT_STRUCT(astl_get_metrics_params_t, params, .flags = 0, .target_handle = target, .metrics = metrics.data(),
                   .metric_count = &count);
  Check(astlGetMetricsOnTarget(&params), "astlGetMetricsOnTarget");
  metrics.resize(count);
  for (const auto& metric : metrics) {
    std::cout << "metric=" << metric.name << '\n';
  }
  return metrics;
}

auto IsPerCoreUtilization(std::string_view name) -> bool {
  constexpr std::string_view prefix = "stat.cpu";
  constexpr std::string_view suffix = ".utilization";
  if (!name.starts_with(prefix) || !name.ends_with(suffix) || name.size() <= prefix.size() + suffix.size()) {
    return false;
  }
  const auto index = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
  return std::ranges::all_of(index, [](char character) { return character >= '0' && character <= '9'; });
}

auto GetSamples(astl_target_handle_t target, astl_metric_handle_t metric) -> std::vector<astl_sample_t> {
  // The checked count API initializes this output; cppcheck cannot track nested output pointers.
  uint32_t count;  // NOLINT(cppcoreguidelines-init-variables)
  ASTL_INIT_STRUCT(astl_get_metric_sample_count_on_target_params_t, count_params, .flags = 0, .target_handle = target,
                   .metric_handle = metric, .sample_count = &count, .start_ts = 0, .end_ts = 0);
  Check(astlGetMetricSampleCountOnTarget(&count_params), "astlGetMetricSampleCountOnTarget");
  // CPU utilization needs an initial baseline; subsequent reads must produce real samples.
  // cppcheck cannot follow output pointers nested in const API parameter structs.
  // cppcheck-suppress uninitvar
  Require(count >= kReadCount - 1, "Too few procfs samples");
  std::vector<astl_sample_t> samples(count);
  ASTL_INIT_STRUCT(astl_get_metric_samples_on_target_params_t, params, .flags = 0, .target_handle = target,
                   .metric_handle = metric, .samples = samples.data(), .sample_count = &count, .start_ts = 0,
                   .end_ts = 0);
  Check(astlGetMetricSamplesOnTarget(&params), "astlGetMetricSamplesOnTarget");
  samples.resize(count);
  return samples;
}

auto ReadValue(const astl_sample_t& sample, astl_value_type_t type) -> double {
  switch (type) {
    case ASTL_VALUE_UINT64:
      return static_cast<double>(sample.value.ui64);
    case ASTL_VALUE_FLOAT64:
      return sample.value.fp64;
    default:
      throw std::runtime_error{"Unexpected procfs value type"};
  }
}

auto SelectMetrics(const std::vector<astl_metric_props_t>& metrics) -> std::vector<astl_metric_props_t> {
  constexpr std::array<std::string_view, 4> memory_names{"meminfo.MemTotal", "meminfo.MemUsed", "meminfo.MemAvailable",
                                                         "meminfo.utilization"};
  std::vector<astl_metric_props_t>          selected;
  for (const auto& metric : metrics) {
    const std::string_view name{metric.name};
    if (IsPerCoreUtilization(name) || std::ranges::find(memory_names, name) != memory_names.end()) {
      selected.push_back(metric);
    }
  }
  for (const auto required : memory_names) {
    Require(std::ranges::any_of(selected,
                                [required](const auto& metric) { return std::string_view{metric.name} == required; }),
            std::string{"Missing metric: "} + std::string{required});
  }
  Require(std::ranges::any_of(selected, [](const auto& metric) { return IsPerCoreUtilization(metric.name); }),
          "No per-core CPU utilization metrics discovered");
  return selected;
}

void CollectMetrics(astl_target_handle_t target, const std::vector<astl_metric_props_t>& selected) {
  std::vector<astl_metric_handle_t> handles;
  std::ranges::transform(selected, std::back_inserter(handles), [](const auto& metric) { return metric.handle; });
  ASTL_INIT_STRUCT(astl_collection_params_t, collection, .flags = 0, .sampling_interval = 0,
                   .collection_mode = ASTL_COLLECTION_MODE_IMMEDIATE);
  ASTL_INIT_STRUCT(astl_configure_metric_collection_on_target_params_t, configure, .flags = 0, .target_handle = target,
                   .collection_params = &collection, .metric_handles = handles.data(),
                   .metric_count = static_cast<uint32_t>(handles.size()));
  Check(astlConfigureMetricCollectionOnTarget(&configure), "astlConfigureMetricCollectionOnTarget");
  ASTL_INIT_STRUCT(astl_read_immediate_on_target_params_t, read, .flags = 0, .target_handle = target);
  for (uint32_t index = 0; index < kReadCount; ++index) {
    if (index != 0) {
      std::this_thread::sleep_for(kReadInterval);
    }
    Check(astlReadImmediateOnTarget(&read), "astlReadImmediateOnTarget");
  }
}

void ValidateMetrics(astl_target_handle_t target, const std::vector<astl_metric_props_t>& selected) {
  double memory_total{};
  for (const auto& metric : selected) {
    if (std::string_view{metric.name} == "meminfo.MemTotal") {
      memory_total = ReadValue(GetSamples(target, metric.handle).back(), metric.value_type);
    }
  }
  Require(std::isfinite(memory_total) && memory_total > 0, "Invalid total system memory");
  for (const auto& metric : selected) {
    const bool is_percent = IsPerCoreUtilization(metric.name) || std::string_view{metric.name} == "meminfo.utilization";
    Require(metric.units == (is_percent ? ASTL_UNITS_PERCENT : ASTL_UNITS_BYTES), "Unexpected metric units");
    uint64_t previous_timestamp{};
    for (const auto& sample : GetSamples(target, metric.handle)) {
      const double value = ReadValue(sample, metric.value_type);
      Require(sample.timestamp > previous_timestamp, "Sample timestamps did not advance");
      previous_timestamp = sample.timestamp;
      Require(std::isfinite(value) && value >= 0 && value <= (is_percent ? 100 : memory_total),
              "Invalid procfs reading");
      std::cout << metric.name << " timestamp=" << sample.timestamp << " value=" << value << '\n';
    }
  }
}

void Run() {
  const auto* const target   = GetProcfsTarget();
  const auto        selected = SelectMetrics(GetMetrics(target));
  CollectMetrics(target, selected);
  ValidateMetrics(target, selected);
  std::cout << "PASS: repeated per-core CPU and system memory collection through ASTL's public API\n";
}

}  // namespace

int main() {
  try {
    Run();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
