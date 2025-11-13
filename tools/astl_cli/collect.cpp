/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#include "collect.hpp"

#include <iostream>
#include <ranges>
#include <string>
#include <thread>

#include "app_config.hpp"
#include "astl/astl_telemetry.h"
#include "astl_metrics.hpp"
#include "external/gplot++.h"
#include "run_workload.hpp"
#include "utils.hpp"

static void PrintCommandConfig(const AppConfig& cfg) {
  if (!cfg.verbose) {
    return;
  }
  std::cout << "[collect]\n";
  std::cout << "  sampling_interval   : "
            << (cfg.collect.sampling_interval ? std::to_string(cfg.collect.sampling_interval->count()) : "(unset)")
            << " ms\n";
  std::cout << "  duration   : " << (cfg.collect.duration ? std::to_string(cfg.collect.duration->count()) : "(unset)")
            << " s\n";
  std::cout << "  metrics    : ";
  if (cfg.collect.metrics.empty()) {
    std::cout << "(none)\n";
  } else {
    for (size_t index = 0; index < cfg.collect.metrics.size(); ++index) {
      const auto& metric_name = cfg.collect.metrics[index];
      std::cout << (index ? ", " : "") << metric_name;
    }
  }
  std::cout << "  workload   : ";
  if (cfg.collect.workload.empty()) {
    std::cout << "(none)\n";
  } else {
    for (size_t index = 0; index < cfg.collect.workload.size(); ++index) {
      std::cout << (index ? " " : "") << cfg.collect.workload[index];
    }
    std::cout << "\n";
  }
}

auto OrganizeSelectedMetricsByTarget(const std::vector<Metric>& all_metrics, const AppConfig& cfg)
    -> std::map<astl_target_handle_t, std::vector<astl_metric_handle_t>> {
  std::map<astl_target_handle_t, std::vector<astl_metric_handle_t>> metrics_by_target;

  for (const auto& metric : all_metrics) {
    for (const auto& [target_handle, target_name] : metric.target_handles_and_names) {
      if (std::ranges::find(cfg.collect.metrics, metric.properties._name) != cfg.collect.metrics.end()) {
        metrics_by_target[target_handle].push_back(metric.handle);
      } else {
        if (cfg.verbose) {
          std::cout << "Skipping metric for target " << target_name << ": " << metric.properties._name << "\n";
        }
      }
    }
  }
  return metrics_by_target;
}

static void CollectTelemetryDuringWorkloadExecution(const AppConfig& cfg) {
  // @todo(ASTL-216) - make this workload echo output to a file, or to the console if --verbose only
  if (!cfg.collect.workload.empty()) {
    std::string command;
    for (const auto& arg : cfg.collect.workload) {
      command += arg + " ";
    }
    if (cfg.verbose) {
      std::cout << "Launching workload: " << command << "\n";
    }
    std::optional<std::chrono::seconds> timeout;
    if (cfg.collect.duration) {
      timeout = *cfg.collect.duration;
    }
    int ret = RunWorkload(command, timeout);
    if (ret != 0) {
      std::cerr << "Workload exited with code: " << ret << "\n";
      return;
    }
  } else {
    auto sleep_duration = cfg.collect.duration ? *cfg.collect.duration : CollectCfg::kDefaultDuration;
    if (cfg.verbose) {
      std::cout << "No workload specified, sleeping for " << sleep_duration << "...\n";
    }
    std::this_thread::sleep_for(sleep_duration);
  }
}

// plot with gplotpp
static void PlotMetricSamples(const AppConfig& cfg, std::chrono::duration<double, std::micro> starting_time_point,
                              const Metric& metric, const std::string& target_name,
                              const std::vector<astl_metric_sample_t>& samples) {
  const std::string& metric_name = metric.properties._name;
  auto               units_str   = UnitsToString(metric.properties._units);
  if (cfg.verbose) {
    std::cout << "Plotting " << samples.size() << " samples for metric " << metric_name << " on target " << target_name
              << "\n";
  }

  if (samples.empty()) {
    std::cout << "No samples to plot for metric " << metric_name << " on target " << target_name << "\n";
    return;
  }
  // Use gplotpp to plot the samples
  // @todo(ASTL-217) - evaluate different plotting libraries. we need:
  // - cross platform
  // - permisssive license
  // - support for png/svg output
  // - support for live update in GUI (nice to have)
  // - support for terminal output (nice to have)
  Gnuplot gnuplot{};
  gnuplot.set_title(metric_name + " | " + target_name);
  gnuplot.set_xlabel("Time (s)");
  gnuplot.set_ylabel(std::string("Value (") + std::string(units_str) + std::string(")"));
  // ensure output_dir exists
  std::filesystem::create_directories(cfg.collect.output_dir);
  std::string output_file = cfg.collect.output_dir + "/" + metric_name + "_" + target_name;
  switch (cfg.collect.plot_type) {
    case CollectCfg::PlotType::TERMINAL:
      std::cout << "Plotting to terminal\n";
      gnuplot.redirect_to_dumb();
      break;
    case CollectCfg::PlotType::PNG:
      output_file.append(".png");
      std::cout << "Plotting to PNG file: " << output_file << "\n";
      gnuplot.redirect_to_png(output_file, "800,600");
      break;
    case CollectCfg::PlotType::SVG: {
      output_file.append(".svg");
      std::cout << "Plotting to SVG file: " << output_file << "\n";
      // gnuplot.redirect_to_svg(); uses 'set terminal svg standalone' which seems to cause issues.
      // so we break it out into manual gnuplot commands here, minus the 'standalone' option.
      // possibly worth investigating to better support mouse-based SVG interractivity
      std::stringstream outstream;
      outstream << "set terminal svg enhanced mouse size " << "800,600" << "\n"
                << "set output '" << output_file << "'\n";
      gnuplot.sendcommand(outstream);
      // gnuplot.redirect_to_svg(output_file, "800,600");
      break;
    }
    case CollectCfg::PlotType::NONE:
      // should not reach here due to earlier check
      return;
  }

  for (const auto& sample : samples) {
    const auto sample_time_point = std::chrono::duration<double, std::micro>(static_cast<double>(sample._timestamp));
    const auto microseconds_since_start = (sample_time_point - starting_time_point);
    const auto seconds_since_start      = std::chrono::duration<double>(microseconds_since_start);
    double     y_plot_value             = AstlValueAsDouble(sample._value, metric.properties._value_type);
    gnuplot.add_point(seconds_since_start.count(), y_plot_value);
  }
  gnuplot.plot("", Gnuplot::LineStyle::STEPS);
  gnuplot.show();
}

auto GetSamples(astl_target_handle_t target_handle, astl_metric_handle_t metric_handle)
    -> std::vector<astl_metric_sample_t> {
  uint32_t sample_count = 0;
  auto     result       = astlGetMetricSampleCountOnTarget(target_handle, metric_handle, &sample_count);
  if (result != ASTL_STATUS_SUCCESS) {
    std::cerr << "Error getting metric sample count on target: " << astlStatusString(result) << "\n";
    return {};
  }
  if (sample_count == 0) {
    return {};
  }
  std::vector<astl_metric_sample_t> samples(sample_count);
  std::ranges::for_each(samples, [](astl_metric_sample_t& sample) { sample._size = sizeof(astl_metric_sample_t); });
  result = astlGetMetricSamplesOnTarget(target_handle, metric_handle, samples.data(), &sample_count);
  if (result != ASTL_STATUS_SUCCESS) {
    std::cerr << "Error getting metric samples on target: " << astlStatusString(result) << "\n";
    return {};
  }
  return samples;
}

auto GetSamplesAndPlot(const AppConfig& cfg, const auto& starting_time_point, std::vector<Metric> const& metrics)
    -> int {
  // enumerate all metrics, and get all samples on all targets for each metric.
  // create one plot of all samples for each metric/target pair.
  for (const auto& metric : metrics) {
    for (const auto& [target_handle, target_name] : metric.target_handles_and_names) {
      auto samples = GetSamples(target_handle, metric.handle);
      if (cfg.verbose) {
        // print samples
        std::cout << "Collected " << samples.size() << " samples for metric " << metric.properties._name << "\n";
        std::ranges::for_each(samples, [](const astl_metric_sample_t& sample) {
          std::cout << "  timestamp: " << sample._timestamp << " ms, value: " << sample._value.fp64 << "\n";
        });
      }
      // plot samples
      if (samples.empty() || cfg.collect.plot_type == CollectCfg::PlotType::NONE) {
        continue;
      }

      // @todo(ASTL-218) - support multiplot based on configuration settings
      // @todo(ASTL-219) - support axis scaling based on configuration settings
      // @todo(ASTL-219) - make image size configurable
      // @todo(ASTL-220) - support multiple output types types at once (e.g., svg + perfetto)
      // @todo(ASTL-221) - support plotting samples from previous runs, maybe stored in CSV files from output manager
      PlotMetricSamples(cfg, starting_time_point, metric, target_name, samples);
    }
  }
  return 0;
}

int Collect(const AppConfig& cfg) {
  PrintCommandConfig(cfg);
  auto metrics_or_error = DetectMetrics(cfg);
  if (!metrics_or_error) {
    std::cerr << "Error detecting metrics: " << astlStatusString(metrics_or_error.error()) << "\n";
    return -1;
  }
  auto metrics_by_target = OrganizeSelectedMetricsByTarget(*metrics_or_error, cfg);
  auto sampling_interval = cfg.collect.sampling_interval.value_or(CollectCfg::kDefaultInterval).count();

  astl_collection_parameters_t collection_params = {._size              = sizeof(astl_collection_parameters_t),
                                                    ._sampling_interval = static_cast<uint32_t>(sampling_interval),
                                                    ._collection_mode   = ASTL_COLLECTION_MODE_SAMPLING,
                                                    ._optimization      = ASTL_COLLECTION_OPTIMIZATION_OVERHEAD};
  // for each target, configure collection on all its selected metrics
  astl_status_code result = ASTL_STATUS_SUCCESS;
  for (auto& [target_handle, metrics] : metrics_by_target) {
    // Configure collection
    // @todo(ASTL-197) - it seems ASTL can configure collection on exactly one target at a time. whatever is the last
    // one configured "wins". we should allow configuration of collection on mutliple targets before starting collection
    // on all of them.
    result = astlConfigureMetricCollectionOnTarget(target_handle, &collection_params, metrics.data(),
                                                   static_cast<uint32_t>(metrics.size()));
    if (result != ASTL_STATUS_SUCCESS) {
      std::cerr << "Error configuring metric collection on target: " << astlStatusString(result) << "\n";
      return -1;
    }
  }
  const auto starting_time_point = std::chrono::steady_clock::now().time_since_epoch();

  for (auto& [target_handle, metrics] : metrics_by_target) {
    result = astlStartCollectionOnTarget(target_handle);
    if (result != ASTL_STATUS_SUCCESS) {
      std::cerr << "Error starting metric collection on target: " << astlStatusString(result) << "\n";
      return -1;
    }
  }

  // launch workload, if any
  CollectTelemetryDuringWorkloadExecution(cfg);

  // stop collection
  for (auto& [target_handle, metrics] : metrics_by_target) {
    result = astlStopCollectionOnTarget(target_handle);
    if (result != ASTL_STATUS_SUCCESS) {
      std::cerr << "Error stopping metric collection on target: " << astlStatusString(result) << "\n";
      return -1;
    }
  }
  return GetSamplesAndPlot(cfg, starting_time_point, *metrics_or_error);
}
