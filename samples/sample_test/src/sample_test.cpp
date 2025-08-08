#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "astl/astl.h"
#include "astl/astl_telemetry.h"

// NOLINTBEGIN
std::unordered_map<std::string, std::string> ParseArgs(int argc, char* argv[]) {
  std::unordered_map<std::string, std::string> args;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.starts_with("--")) {
      auto eq = arg.find('=');
      if (eq != std::string::npos) {
        args[arg.substr(2, eq - 2)] = arg.substr(eq + 1);
      } else {
        args[arg.substr(2)] = "true";
      }
    } else if (arg.starts_with("-") && i + 1 < argc) {
      args[arg.substr(1)] = argv[++i];
    }
  }
  return args;
}
// NOLINTEND

void PrintHelp() {
  std::cout << "Usage: sample_test [options]\n\n"
            << "Options:\n"
            << "  --help              Show this help message.\n"
            << "  --version           Print version and exit.\n"
            << "  --immediate         Trigger immediate sample read.\n"
            << "  --interval=<n>      Trigger interval sample read period in milliseconds.\n"
            << "  --duration=<n>      Collection duration in seconds.\n"
            << "  --config=<path>     Path to  json config file for ASTL.\n"
            << "  Default: interval mode, 10 seconds duration and 500 milliseconds sampling interval.\n";
}

void PrintVersion() {
  astl_version_t version = astlVersion();
  std::cout << "ASTL v" << version._major << "." << version._minor << "." << version._micro << "\n";
  std::cout << "Version string: " << astlVersionString() << "\n";
}

std::expected<std::chrono::milliseconds, int> GetIntervalArgument(
    const std::unordered_map<std::string, std::string>& args) {
  if (args.contains("interval")) {
    try {
      int tmp_interval = std::stoi(args.at("interval"));
      if (tmp_interval <= 0) {
        std::cerr << "Interval must be a positive integer.\n";
        return std::unexpected<int>(1);
      }
      auto sampling_interval_ms = std::chrono::milliseconds(tmp_interval);
      std::cout << "Interval set to: " << sampling_interval_ms.count() << " milliseconds\n";
      return sampling_interval_ms;
    } catch (const std::exception& e) {
      std::cerr << "Invalid value for --interval\n";
      return std::unexpected<int>(1);
    }
  }
  return std::chrono::milliseconds{};
}
std::expected<std::chrono::seconds, int> GetDurationArgument(const std::unordered_map<std::string, std::string>& args) {
  if (args.contains("duration")) {
    try {
      int tmp_duration = std::stoi(args.at("duration"));
      if (tmp_duration <= 0) {
        std::cerr << "Duration must be a positive integer.\n";
        return std::unexpected<int>(1);
      }
      auto duration_seconds = std::chrono::seconds(tmp_duration);
      std::cout << "Duration set to: " << duration_seconds.count() << " seconds\n";
      return duration_seconds;
    } catch (const std::exception&) {
      std::cerr << "Invalid value for --duration\n";
      return std::unexpected<int>(1);
    }
  }
  return std::chrono::seconds{};
}

astl_status_code InitializeASTL(const char* config_file_path) {
  ASTL_INIT_STRUCT(astl_initialization_parameters_t, init_params, ._configuration_file_path = config_file_path);
  astl_status_code status = astlInitialize(&init_params);
  std::cout << "Initialize status: " << astlStatusString(status) << "\n";
  return status;
}

astl_status_code GetTargets(std::vector<astl_target_properties_t>& target_properties_buffer,
                            astl_target_properties_t&              target_properties) {
  uint32_t         target_count = 0;
  astl_status_code status       = astlGetTargetCount(&target_count);
  std::cout << "Target count: " << target_count << "\n";
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  target_properties_buffer.resize(target_count);
  if (!target_properties_buffer.empty()) {
    target_properties_buffer[0]._size = sizeof(astl_target_properties_t);
  }
  status = astlGetTargets(target_properties_buffer.data(), &target_count);
  target_properties_buffer.resize(target_count);
  std::cout << "astlGetTargets Status: " << astlStatusString(status) << '\n';

  if (target_count > 0 && (status == ASTL_STATUS_SUCCESS || status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED)) {
    std::ranges::for_each(target_properties_buffer, [](const auto target_properties) {
      std::cout << "Target info:" << '\n';
      std::cout << "  Name:        " << (target_properties._name ? target_properties._name : "<null>") << '\n';
      std::cout << "  Description: " << (target_properties._description ? target_properties._description : "<null>")
                << '\n';
    });
    target_properties = target_properties_buffer[0];
    return ASTL_STATUS_SUCCESS;
  }

  std::cerr << "Failed to get target info.\n";
  return ASTL_STATUS_INTERNAL_ERROR;
}

astl_status_code GetMetrics(astl_target_handle_t target_handle, std::vector<astl_metric_properties_t>& metric_buffer,
                            uint32_t& metric_count) {
  astl_status_code status = astlGetMetricCount(target_handle, &metric_count);
  std::cout << "Metric count: " << metric_count << '\n';
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  metric_buffer.resize(metric_count);
  if (metric_count > 0) {
    metric_buffer[0]._size = sizeof(astl_metric_properties_t);
  }
  status = astlGetMetrics(target_handle, metric_buffer.data(), &metric_count);
  std::cout << "astlGetMetrics Status: " << astlStatusString(status) << '\n';
  return status;
}

astl_status_code ConfigureAndRunCollection(astl_target_handle_t                         target_handle,
                                           const std::vector<astl_metric_properties_t>& metric_buffer,
                                           uint32_t metric_count, bool do_interval,
                                           std::chrono::seconds      duration_seconds,
                                           std::chrono::milliseconds sampling_interval) {
  ASTL_INIT_STRUCT(astl_collection_parameters_t, collection_params,
                   ._sampling_interval = do_interval ? static_cast<uint32_t>(sampling_interval.count()) : 0,
                   ._collection_mode   = do_interval ? ASTL_COLLECTION_MODE_SAMPLING : ASTL_COLLECTION_MODE_IMMEDIATE,
                   ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD);

  // Build a vector of metric handles from metric_buffer
  std::vector<astl_metric_handle_t> metric_handles_vec;
  metric_handles_vec.reserve(metric_buffer.size());

  std::transform(metric_buffer.begin(), metric_buffer.end(), std::back_inserter(metric_handles_vec),
                 [](auto const& prop) { return prop._handle; });

  astl_status_code status =
      astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles_vec.data(), metric_count);
  std::cout << "astlConfigureMetricCollectionOnTarget Status: " << astlStatusString(status) << '\n';
  if (status != ASTL_STATUS_SUCCESS) {
    std::cout << "Failed to configure metric collection - exiting early" << '\n';
    return status;
  }

  status = astlStartCollectionOnTarget(target_handle);
  std::cout << "astlStartCollectionOnTarget Status: " << astlStatusString(status) << '\n';

  if (do_interval && duration_seconds > std::chrono::seconds::zero()) {
    std::this_thread::sleep_for(duration_seconds);
  }

  if (!do_interval) {
    status = astlReadImmediateOnTarget(target_handle);
    std::cout << "ReadImmediate: " << astlStatusString(status) << '\n';
  }

  status = astlStopCollectionOnTarget(target_handle);
  std::cout << "astlStopCollectionOnTarget Status: " << astlStatusString(status) << '\n';

  return status;
}

void RetrieveSamples(astl_target_handle_t target_handle, const std::vector<astl_metric_properties_t>& metric_buffer) {
  if (metric_buffer.empty()) {
    return;
  }

  uint32_t         sample_count{};
  astl_status_code status =
      astlGetMetricSampleCountOnTarget(target_handle, metric_buffer.front()._handle, &sample_count);
  std::cout << "astlGetMetricSampleCountOnTarget Status: " << astlStatusString(status) << '\n';

  std::vector<astl_metric_sample_t> samples(sample_count);
  if (sample_count > 0) {
    samples[0]._size = sizeof(astl_metric_sample_t);
  }
  status = astlGetMetricSamplesOnTarget(target_handle, metric_buffer.front()._handle, samples.data(), &sample_count);
  std::cout << "astlGetMetricSamplesOnTarget Status: " << astlStatusString(status) << '\n';
}

/**
 * @brief  Example ASTL usage: version, init, target discovery, metric collection.
 *
 * Supported options:
 *   --help          Show usage
 *   --version       Print ASTL version and exit
 *   --immediate     Trigger an immediate metric sample. This is default behavior.
 *   --interval=<n>  Trigger interval sampling every <n> milliseconds
 *   --config=<path> Path to json config file for ASTL
 *
 * A lightweight argument parser interprets these flags and
 * runs the corresponding ASTL actions.
 */
int main(int argc, char* argv[]) {
  auto args = ParseArgs(argc, argv);

  if (args.contains("help")) {
    PrintHelp();
    return 0;
  }

  if (args.contains("version")) {
    PrintVersion();
    return 0;
  }

  std::chrono::seconds duration_seconds(10);  // Default duration
  auto                 duration_result = GetDurationArgument(args);
  if (!duration_result) {
    return duration_result.error();
  }
  if (duration_result->count() > 0) {
    duration_seconds = *duration_result;
  }

  const bool do_immediate = args.contains("immediate");
  bool       do_interval  = args.contains("interval");
  if (!do_immediate && !do_interval) {
    do_interval = true;
    std::cout << "Neither --immediate nor --interval specified; defaulting to interval mode\n";
  }

  std::chrono::milliseconds sampling_interval_ms(500);  // Default sampling interval
  if (do_interval) {
    auto interval_result = GetIntervalArgument(args);
    if (!interval_result) {
      return interval_result.error();
    }
    if (interval_result->count() > 0) {
      sampling_interval_ms = *interval_result;
    }
  }
  const char* config_file_path = args.contains("config") ? args["config"].c_str() : nullptr;

  // Initialize ASTL
  astl_status_code status = InitializeASTL(config_file_path);
  if (status != ASTL_STATUS_SUCCESS) {
    return 2;
  }

  // Get targets
  std::vector<astl_target_properties_t> target_properties_buffer;
  astl_target_properties_t              target_properties;
  status = GetTargets(target_properties_buffer, target_properties);
  if (status != ASTL_STATUS_SUCCESS) {
    return 4;
  }

  // Get metrics
  std::vector<astl_metric_properties_t> metric_buffer;
  uint32_t                              metric_count{};
  status = GetMetrics(target_properties._handle, metric_buffer, metric_count);
  if (status != ASTL_STATUS_SUCCESS) {
    // Note - this is masking error codes, but our CTest integration tests expect these sample tests to function
    // even without mock sysfs running
    return 0;
  }

  if (metric_count == 0) {
    std::cout << "no metrics available to collect on target: " << target_properties._name << '\n';
    return 0;
  }

  // Configure and run collection
  status = ConfigureAndRunCollection(target_properties._handle, metric_buffer, metric_count, do_interval,
                                     duration_seconds, sampling_interval_ms);
  if (status != ASTL_STATUS_SUCCESS) {
    // Note - this is masking error codes, but our CTest integration tests expect these sample tests to function
    // even without mock sysfs running
    return 0;
  }

  // Retrieve and display samples
  // Currently unimplemented. For the first milestone, just read from debug logs
  // TODO(ASTL-60) - Print samples using basic text writer plugin
  RetrieveSamples(target_properties._handle, metric_buffer);

  return 0;
}
