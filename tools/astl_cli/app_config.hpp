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

#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

// app_config.hpp
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "argparse/argparse.hpp"    // https://github.com/p-ranav/argparse
#include "astl_file_interface.hpp"  // for astl_units_t, ParseUnits
#include "astl_utils.hpp"           // for astl_units_t, ParseUnits
#include "toml++/toml.hpp"          // https://github.com/marzer/tomlplusplus

// ---------------- Config model with defaults & descriptions ----------------

/* Configuration for the "list-metrics" command*/
struct ListMetricsCfg {
  std::vector<astl_units_t> units_include_list;  // e.g. [ASTL_UNITS_WATTS, ASTL_UNITS_CELSIUS]
  bool                      name_only = false;

  static constexpr std::string DescUnitsIncludeList() { return "List of unit names to include."; }
  static constexpr std::string DescNameOnly() { return "Print only metric names."; }

  void MergeFromToml(const toml::table& table) {
    if (const auto* arr = table["units-include-list"].as_array()) {
      std::vector<astl_units_t> tmp;
      arr->for_each([&](const toml::node& n) {
        if (auto unit = n.value<std::string>()) {
          tmp.push_back(astl::ParseUnits(*unit));
        }
      });
      units_include_list = std::move(tmp);
    }
    name_only = table["name-only"].value_or<bool>(std::move(name_only));
  }
};

/* Configuration for the "collect" command*/
struct CollectCfg {
  std::optional<std::chrono::milliseconds> sampling_interval;
  constexpr static std::chrono::duration   kDefaultInterval = std::chrono::milliseconds(100);
  std::optional<std::chrono::seconds>      duration;
  constexpr static std::chrono::duration   kDefaultDuration = std::chrono::seconds(10);

  std::vector<std::string> metrics;               // ["IO Power", "SoC Temperature", ...]
  std::string              output_dir{"plots/"};  // output directory for plots

  enum class PlotType { NONE, TERMINAL, PNG, SVG };
  // PlotType::from_string
  static PlotType FromString(const std::string& str) {
    if (str == "terminal") {
      return PlotType::TERMINAL;
    }
    if (str == "png") {
      return PlotType::PNG;
    }
    if (str == "svg") {
      return PlotType::SVG;
    }
    return PlotType::NONE;
  }

  PlotType plot_type{PlotType::NONE};  // plot output file type

  std::vector<std::string> workload;  // ["prog","arg1","arg2",...]

  static constexpr std::string DescSamplingInterval() { return "Collection sampling interval in milliseconds."; }
  static constexpr std::string DescDuration() { return "Collection duration in seconds."; }
  static constexpr std::string DescMetricsCli() {
    return "One metric (from list-metrics) to collect. This option can be repeated";
  }
  static constexpr std::string DescMetricsToml() { return "List of metrics (from list-metrics) to collect. "; }
  static constexpr std::string DescOutputDir() { return "Output directory for plots."; }
  static constexpr std::string DescPlotType() {
    return "Plot output file type [none, terminal, png, or svg]. If not 'none', requires gnuplot.";
  }
  static constexpr std::string DescWorkload() { return "Workload program and arguments as array."; }

  void MergeFromToml(const toml::table& table) {
    if (auto val = table["sampling_interval"].value<uint32_t>()) {
      sampling_interval = std::chrono::milliseconds(*val);
    }
    if (auto val = table["duration"].value<uint32_t>()) {
      duration = std::chrono::seconds(*val);
    }
    if (const auto* arr = table["metrics"].as_array()) {
      std::vector<std::string> tmp;
      arr->for_each([&](const toml::node& n) {
        if (auto metric = n.value<std::string>()) {
          tmp.push_back(*metric);
        }
      });
      metrics = std::move(tmp);
    }
    if (auto val = table["output-dir"].value<std::string>()) {
      // ensure output dir ends with '/'
      std::string dir = *val;
      if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir.push_back('/');
      }
      output_dir = dir;
    }
    if (auto val = table["plot-type"].value<std::string>()) {
      plot_type = CollectCfg::FromString(*val);
    }
    if (const auto* arr = table["workload"].as_array()) {
      std::vector<std::string> tmp;
      arr->for_each([&](const toml::node& n) {
        if (auto workload_arg = n.value<std::string>()) {
          tmp.push_back(*workload_arg);
        }
      });
      workload = std::move(tmp);
    }
  }
};

/* Configuration for the "list-counters" command*/
struct ListCountersCfg {
  bool name_only = false;

  static constexpr std::string DescNameOnly() { return "Print only counter names."; }

  void MergeFromToml(const toml::table& table) { name_only = table["name-only"].value_or<bool>(std::move(name_only)); }
};

/* Configuration for the "read-counter" command */
struct ReadCounterCfg {
  std::string counter_name;  // name of the counter to read

  static constexpr std::string DescCounterName() { return "Name of the counter to read."; }

  void MergeFromToml(const toml::table& table) {
    if (auto val = table["counter-name"].value<std::string>()) {
      counter_name = *val;
    }
  }
};

struct AppConfig {
  enum class Command { COLLECT, LIST_METRICS, LIST_COUNTERS, READ_COUNTER };
  // top-level
  std::filesystem::path config_file;                     // path to TOML
  Command               command{Command::LIST_METRICS};  // default command if none given ("collect" | "list-metrics")
  std::filesystem::path astl_config_file;  // path to ASTL config file (defaults to '~/.astl_configuration.json')
  bool                  verbose{false};    // toggle verbose stdout output

  // sections
  ListMetricsCfg  list;
  CollectCfg      collect;
  ListCountersCfg list_counters;
  ReadCounterCfg  read_counter;

  static constexpr Command ParseCommand(std::string_view cmd_str) {
    if (cmd_str == "collect") {
      return Command::COLLECT;
    }
    if (cmd_str == "list-metrics") {
      return Command::LIST_METRICS;
    }
    if (cmd_str == "list-counters") {
      return Command::LIST_COUNTERS;
    }
    if (cmd_str == "read-counter") {
      return Command::READ_COUNTER;
    }
    return Command::COLLECT;  // default
  }

  static constexpr std::string DescConfigFile() { return "Path to TOML configuration file."; }
  static constexpr std::string DescCommand() { return "Default subcommand if none passed on CLI."; }
  static constexpr std::string DescAstlConfigFile() { return "Path to ASTL configuration file."; }
  static constexpr std::string DescVerbose() { return "Enable verbose console output."; }

  void MergeFromToml(const toml::table& table) {
    if (auto val = table["command"].value<std::string>()) {
      command = ParseCommand(*val);
    }
    if (const auto* val = table["list-metrics"].as_table()) {
      list.MergeFromToml(*val);
    }
    if (const auto* val = table["collect"].as_table()) {
      collect.MergeFromToml(*val);
    }
    if (const auto* val = table["list-counters"].as_table()) {
      list_counters.MergeFromToml(*val);
    }
    if (const auto* val = table["read-counter"].as_table()) {
      read_counter.MergeFromToml(*val);
    }
    // config_file is absent from the config file because you can only specify the config file from the command line
    if (auto val = table["astl_config_file"].value<std::string>()) {
      auto path_parse_result = astl::ExpandFilePath(*val);
      astl_config_file       = path_parse_result.value_or(astl_config_file);
      if (!path_parse_result) {
        std::cerr << path_parse_result.error() << "\n";
      }
    }
    if (auto val = table["verbose"].value<bool>()) {
      verbose = *val;
    }
  }
};

namespace std {

inline auto to_string(const AppConfig::Command cmd) -> std::string {
  switch (cmd) {
    case AppConfig::Command::COLLECT:
      return "collect";
    case AppConfig::Command::LIST_METRICS:
      return "list-metrics";
    case AppConfig::Command::LIST_COUNTERS:
      return "list-counters";
    case AppConfig::Command::READ_COUNTER:
      return "read-counter";
    default:
      return "unknown";
  }
}

// ostream operator for Command
inline auto operator<<(std::ostream& output_stream, const AppConfig::Command cmd) -> std::ostream& {
  output_stream << to_string(cmd);
  return output_stream;
}

}  // namespace std

#endif  // APP_CONFIG_HPP