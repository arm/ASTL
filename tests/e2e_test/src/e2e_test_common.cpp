/**
 * @file e2e_test_common.cpp
 * @brief Implementation of common E2E test utilities
 */

#include "e2e_test_common.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace astl_test {

auto CheckMockSysfs(const std::string& sysfs_root) -> bool {
  if (!fs::exists(sysfs_root)) {
    std::cerr << "❌ MockSysfs not found: " << sysfs_root << std::endl;
    std::cerr << "Please start MockSysfs: ./scripts/launch_mocksysfs.sh" << std::endl;
    return false;
  }
  std::cout << "✓ MockSysfs accessible at " << sysfs_root << std::endl;
  return true;
}

auto GetTargetByName(const std::string& target_name, astl_target_properties_t& target_properties) -> bool {
  uint32_t target_count = 0;
  auto     status       = astlGetTargetCount(&target_count);
  if (status != ASTL_STATUS_SUCCESS || target_count == 0) {
    std::cerr << "No targets found" << std::endl;
    return false;
  }

  std::vector<astl_target_properties_t> targets(target_count);
  if (!targets.empty()) {
    targets[0]._size = sizeof(astl_target_properties_t);
  }

  status = astlGetTargets(targets.data(), &target_count);
  if (status != ASTL_STATUS_SUCCESS && status != ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED) {
    std::cerr << "Failed to get targets" << std::endl;
    return false;
  }

  // Search for target by name
  for (uint32_t i = 0; i < target_count; ++i) {
    if (targets[i]._name && target_name == targets[i]._name) {
      target_properties = targets[i];
      std::cout << "✓ Using target: " << target_properties._name << std::endl;
      return true;
    }
  }

  // Target not found - list available targets
  std::cerr << "Target '" << target_name << "' not found" << std::endl;
  std::cerr << "Available targets (" << target_count << "):" << std::endl;
  for (uint32_t i = 0; i < target_count; ++i) {
    std::cerr << "  [" << i << "] " << (targets[i]._name ? targets[i]._name : "<unnamed>") << std::endl;
  }
  return false;
}

auto GetMetrics(astl_target_handle_t target_handle, std::vector<astl_metric_handle_t>& metric_handles,
                std::vector<std::string>& metric_names) -> bool {
  uint32_t metric_count = 0;
  auto     status       = astlGetMetricCount(target_handle, &metric_count);
  if (status != ASTL_STATUS_SUCCESS || metric_count == 0) {
    std::cerr << "No metrics found" << std::endl;
    return false;
  }

  std::vector<astl_metric_properties_t> metrics(metric_count);
  if (!metrics.empty()) {
    metrics[0]._size = sizeof(astl_metric_properties_t);
  }

  status = astlGetMetrics(target_handle, metrics.data(), &metric_count);
  if (status != ASTL_STATUS_SUCCESS) {
    std::cerr << "Failed to get metrics" << std::endl;
    return false;
  }

  // Limit to first 5 metrics for test simplicity
  size_t metrics_to_use = std::min<size_t>(metric_count, 5);
  std::cout << "✓ Will collect " << metrics_to_use << " metric(s)" << std::endl;

  for (size_t i = 0; i < metrics_to_use; ++i) {
    metric_handles.push_back(metrics[i]._handle);
    metric_names.push_back(metrics[i]._name ? metrics[i]._name : "<unnamed>");
    std::cout << "  - " << metric_names[i] << std::endl;
  }

  return true;
}

auto RetrieveSamples(astl_target_handle_t target_handle, const std::vector<astl_metric_handle_t>& metric_handles,
                     const std::vector<std::string>& metric_names) -> uint32_t {
  uint32_t total_samples = 0;

  for (size_t i = 0; i < metric_handles.size(); ++i) {
    uint32_t sample_count{};
    auto     status = astlGetMetricSampleCountOnTarget(target_handle, metric_handles[i], &sample_count);

    if (status == ASTL_STATUS_SUCCESS && sample_count > 0) {
      total_samples += sample_count;
      std::cout << "  " << metric_names[i] << ": " << sample_count << " samples" << std::endl;
    }
  }

  return total_samples;
}

}  // namespace astl_test
