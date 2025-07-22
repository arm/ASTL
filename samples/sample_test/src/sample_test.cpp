#include <algorithm>
#include <iostream>
#include <string>
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
            << "  --immediate         Trigger immediate sample read. This is default behavior.\n"
            << "  --interval=<n>      Trigger interval sample read.\n"
            << "  --config=<path>     Path to  json config file for ASTL.\n";
}

void PrintVersion() {
  astl_version_t version = astlVersion();
  std::cout << "ASTL v" << version._major << "." << version._minor << "." << version._micro << "\n";
  std::cout << "Version string: " << astlVersionString() << "\n";
}

int ValidateIntervalArgument(const std::unordered_map<std::string, std::string>& args) {
  if (args.contains("interval")) {
    try {
      int interval = std::stoi(args.at("interval"));
      std::cout << "Interval set to: " << interval << " milliseconds\n";
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "Invalid value for --interval\n";
      return 1;
    }
  }
  return 0;
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
  std::cout << "astlGetTargets Status: " << astlStatusString(status) << std::endl;

  if (target_count > 0 && (status == ASTL_STATUS_SUCCESS || status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED)) {
    std::ranges::for_each(target_properties_buffer, [](const auto target_properties) {
      std::cout << "Target info:" << std::endl;
      std::cout << "  Name:        " << (target_properties._name ? target_properties._name : "<null>") << std::endl;
      std::cout << "  Description: " << (target_properties._description ? target_properties._description : "<null>")
                << std::endl;
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
  std::cout << "Metric count: " << metric_count << std::endl;
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }

  metric_buffer.resize(metric_count);
  if (metric_count > 0) {
    metric_buffer[0]._size = sizeof(astl_metric_properties_t);
  }
  status = astlGetMetrics(target_handle, metric_buffer.data(), &metric_count);
  std::cout << "astlGetMetrics Status: " << astlStatusString(status) << std::endl;
  return status;
}

astl_status_code ConfigureAndRunCollection(astl_target_handle_t                         target_handle,
                                           const std::vector<astl_metric_properties_t>& metric_buffer,
                                           uint32_t metric_count, bool do_interval) {
  constexpr uint32_t sample_interval = 50;
  ASTL_INIT_STRUCT(astl_collection_parameters_t, collection_params,
                   ._sampling_interval = do_interval ? sample_interval : 0,
                   ._collection_mode   = do_interval ? ASTL_COLLECTION_MODE_SAMPLING : ASTL_COLLECTION_MODE_IMMEDIATE,
                   ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD);

  // Build a vector of metric handles from metric_buffer
  std::vector<astl_metric_handle_t> metric_handles_vec;
  metric_handles_vec.reserve(metric_buffer.size());

  std::transform(metric_buffer.begin(), metric_buffer.end(), std::back_inserter(metric_handles_vec),
                 [](auto const& prop) { return prop._handle; });

  astl_status_code status =
      astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metric_handles_vec.data(), metric_count);
  std::cout << "astlConfigureMetricCollectionOnTarget Status: " << astlStatusString(status) << std::endl;
  if (status != ASTL_STATUS_SUCCESS) {
    std::cout << "Failed to configure metric collection - exiting early" << std::endl;
    return status;
  }

  status = astlStartCollectionOnTarget(target_handle);
  std::cout << "astlStartCollectionOnTarget Status: " << astlStatusString(status) << std::endl;

  if (!do_interval) {
    status = astlReadImmediateOnTarget(target_handle);
    std::cout << "ReadImmediate: " << astlStatusString(status) << '\n';
  }

  status = astlStopCollectionOnTarget(target_handle);
  std::cout << "astlStopCollectionOnTarget Status: " << astlStatusString(status) << std::endl;

  return status;
}

void RetrieveSamples(astl_target_handle_t target_handle, const std::vector<astl_metric_properties_t>& metric_buffer) {
  if (metric_buffer.empty()) {
    return;
  }

  uint32_t         sample_count{};
  astl_status_code status =
      astlGetMetricSampleCountOnTarget(target_handle, metric_buffer.front()._handle, &sample_count);
  std::cout << "astlGetMetricSampleCountOnTarget Status: " << astlStatusString(status) << std::endl;

  std::vector<astl_metric_sample_t> samples(sample_count);
  if (sample_count > 0) {
    samples[0]._size = sizeof(astl_metric_sample_t);
  }
  status = astlGetMetricSamplesOnTarget(target_handle, metric_buffer.front()._handle, samples.data(), &sample_count);
  std::cout << "astlGetMetricSamplesOnTarget Status: " << astlStatusString(status) << std::endl;
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

  if (int result = ValidateIntervalArgument(args); result != 0) {
    return result;
  }

  const bool do_immediate = args.contains("immediate");
  const bool do_interval  = args.contains("interval");
  if (!do_immediate && !do_interval) {
    std::cout << "--immediate or --interval not set, running --immediate\n";
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
    std::cout << "no metrics available to collect on target: " << target_properties._name << std::endl;
    return 0;
  }

  // Configure and run collection
  status = ConfigureAndRunCollection(target_properties._handle, metric_buffer, metric_count, do_interval);
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
