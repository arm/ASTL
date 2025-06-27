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
            << "  --interval=<n>      Trigger interval sample read.\n";
}

/**
 * @brief  Example ASTL usage: version, init, target discovery, metric collection.
 *
 * Supported options:
 *   --help         Show usage
 *   --version      Print ASTL version and exit
 *   --immediate    Trigger an immediate metric sample. This is default behavior.
 *   --interval=<n> Trigger interval sampling every <n> milliseconds
 *
 * A lightweight argument parser interprets these flags and
 * runs the corresponding ASTL actions.
 */
int main(int argc, char* argv[]) {
  auto             args = ParseArgs(argc, argv);
  astl_status_code status{};

  if (args.contains("help")) {
    PrintHelp();
    return 0;
  }

  if (args.contains("version")) {
    // 1. Print version
    astl_version_t version = astlVersion();
    std::cout << "ASTL v" << version._major << "." << version._minor << "." << version._micro << "\n";
    std::cout << "Version string: " << astlVersionString() << "\n";
    return 0;
  }

  const bool do_immediate = args.contains("immediate");
  const bool do_interval  = args.contains("interval");
  if (!do_immediate && !do_interval) {
    std::cout << "--immediate or --interval not set, running --immediate\n";
  }

  if (args.contains("interval")) {
    try {
      int interval = std::stoi(args["interval"]);
      std::cout << "Interval set to: " << interval << " milliseconds\n";
    } catch (const std::exception& e) {
      std::cerr << "Invalid value for --interval\n";
      return 1;
    }
  }

  // 1. Initialize
  astl_initialization_parameters_t init_params{};
  status = astlInitialize(&init_params);
  std::cout << "Initialize status: " << astlStatusString(status) << "\n";

  // 2. Get targets
  uint32_t target_count = 0;
  status                = astlGetTargetCount(&target_count);
  std::cout << "Target count: " << target_count << "\n";

  astl_target_properties_t target_properties{};
  target_properties._size = sizeof(astl_target_properties_t);
  status                  = astlGetTargets(&target_properties, &target_count);
  std::cout << "astlGetTargets Status: " << astlStatusString(status) << std::endl;
  if (status == ASTL_STATUS_SUCCESS || status == ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED) {
    std::cout << "Target info:" << std::endl;
    std::cout << "  Name:        " << (target_properties._name ? target_properties._name : "<null>") << std::endl;
    std::cout << "  Description: " << (target_properties._description ? target_properties._description : "<null>")
              << std::endl;
  } else {
    std::cerr << "Failed to get target info.\n";
  }

  // 3. Configure and start collection
  uint32_t metric_count{};
  astlGetMetricCount(target_properties._handle, &metric_count);
  std::cout << "Metric count: " << metric_count << std::endl;

  std::vector<astl_metric_properties_t> metric_buffer(metric_count);
  status = astlGetMetrics(target_properties._handle, metric_buffer.data(), &metric_count);
  std::cout << "astlGetMetrics Status: " << astlStatusString(status) << std::endl;

  constexpr uint32_t           sample_interval = 50;
  astl_collection_parameters_t collection_params{
      ._size              = sizeof(astl_collection_parameters_t),
      ._sampling_interval = do_interval ? sample_interval : 0,
      ._collection_mode   = do_interval ? ASTL_COLLECTION_MODE_SAMPLING : ASTL_COLLECTION_MODE_IMMEDIATE,
      ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD,
  };
  status = astlConfigureMetricCollectionOnTarget(target_properties._handle, &collection_params,
                                                 &metric_buffer.front()._handle, metric_count);
  std::cout << "astlConfigureMetricCollectionOnTarget Status: " << astlStatusString(status) << std::endl;

  status = astlStartCollectionOnTarget(target_properties._handle);
  std::cout << "astlStartCollectionOnTarget Status: " << astlStatusString(status) << std::endl;

  if (!do_interval) {
    status = astlReadImmediateOnTarget(target_properties._handle);
    std::cout << "ReadImmediate: " << astlStatusString(status) << '\n';
  }

  status = astlStopCollectionOnTarget(target_properties._handle);
  std::cout << "astlStopCollectionOnTarget Status: " << astlStatusString(status) << std::endl;

  // 4. Get and print samples
  // Currently unimplemented. For the first milestone, just read from debug logs
  // TODO(ASTL-60) - Print samples using basic text writer plugin
  uint32_t sample_count{};
  status = astlGetMetricSampleCountOnTarget(target_properties._handle, &metric_buffer.front()._handle, &sample_count);
  std::cout << "astlGetMetricSampleCountOnTarget Status: " << astlStatusString(status) << std::endl;

  std::vector<astl_metric_sample_t> samples(sample_count);
  status = astlGetMetricSamplesOnTarget(target_properties._handle, &metric_buffer.front()._handle, samples.data(),
                                        &sample_count);
  std::cout << "astlGetMetricSamplesOnTarget Status: " << astlStatusString(status) << std::endl;

  return 0;
}
