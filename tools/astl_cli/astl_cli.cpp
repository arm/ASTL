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

// main astl_cli.cpp
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "argparse/argparse.hpp"  // https://github.com/p-ranav/argparse
#include "astl_file_interface.hpp"
#include "collect.hpp"
#include "list_counters.hpp"
#include "list_metrics.hpp"
#include "read_counter.hpp"
#include "toml++/toml.hpp"  // https://github.com/marzer/tomlplusplus

// --------------- TOML load/save helpers (with comments) --------------------

static auto LoadTomlIfExists(std::filesystem::path const& path) -> std::optional<toml::table> {
  if (path.empty()) {
    return std::nullopt;
  }
  try {
    return toml::parse_file(path.string());
  } catch (const toml::parse_error& err) {
    std::cerr << "Error parsing TOML file " << path << ": " << err.description() << " at " << err.source().begin
              << "\n";
    return std::nullopt;
  }
}

// save the [list-metrics] section to an output stream
static void SaveTomlWithComments(std::ostream& ofs, const ListMetricsCfg& cfg_list) {
  ofs << "[list-metrics]\n";
  ofs << "\n# " << ListMetricsCfg::DescUnitsIncludeList() << "\n";
  ofs << "units-include-list = [";
  const auto& units = cfg_list.units_include_list;
  for (size_t i = 0; i < units.size(); ++i) {
    ofs << (i ? ", " : "") << "\"" << std::to_string(units[i]) << "\"";
  }
  ofs << "]\n";

  ofs << "\n# " << ListMetricsCfg::DescNameOnly() << "\n";
  ofs << "name-only = " << (cfg_list.name_only ? "true" : "false") << "\n\n";
}

/* Save the [collect] section to an output stream */
static void SaveTomlWithComments(std::ostream& ofs, const CollectCfg& cfg_collect) {
  ofs << "[collect]\n";
  ofs << "# " << CollectCfg::DescSamplingInterval() << "\n";
  if (cfg_collect.sampling_interval) {
    ofs << "sampling_interval = " << cfg_collect.sampling_interval->count() << "\n";
  } else {
    ofs << "# sampling_interval = " << CollectCfg::kDefaultInterval.count() << "\n";
  }

  ofs << "\n# " << CollectCfg::DescDuration() << "\n";
  if (cfg_collect.duration) {
    ofs << "duration = " << cfg_collect.duration->count() << "\n";
  } else {
    ofs << "# duration = " << CollectCfg::kDefaultDuration.count() << "\n";
  }

  ofs << "\n# " << CollectCfg::DescMetricsToml() << "\n";
  ofs << "metrics = [";
  // @todo(ASTL-214) - when saving default config toml, enumerate and describe all available metrics
  for (size_t i = 0; i < cfg_collect.metrics.size(); ++i) {
    ofs << (i ? ", " : "") << "\"" << cfg_collect.metrics[i] << "\"";
  }
  ofs << "]\n";

  // output-dir
  ofs << "\n# " << CollectCfg::DescOutputDir() << "\n";
  ofs << "output-dir = \"" << cfg_collect.output_dir << "\"\n";

  // plot-type
  ofs << "\n# " << CollectCfg::DescPlotType() << "\n";
  std::string plot_type_str = "none";
  switch (cfg_collect.plot_type) {
    case CollectCfg::PlotType::TERMINAL:
      plot_type_str = "terminal";
      break;
    case CollectCfg::PlotType::PNG:
      plot_type_str = "png";
      break;
    case CollectCfg::PlotType::SVG:
      plot_type_str = "svg";
      break;
    default:
      plot_type_str = "none";
      break;
  }
  ofs << "plot-type = \"" << plot_type_str << "\"\n";

  ofs << "\n# " << CollectCfg::DescWorkload() << "\n";
  ofs << "workload = [";
  const auto& workload = cfg_collect.workload;
  for (size_t i = 0; i < workload.size(); ++i) {
    ofs << (i ? ", " : "") << "\"" << workload[i] << "\"";
  }
  ofs << "]\n\n";
}

// Save the [list-counters] section to an output stream
static void SaveTomlWithComments(std::ostream& ofs, const ListCountersCfg& cfg_list_counters) {
  ofs << "[list-counters]\n";
  ofs << "\n# " << ListCountersCfg::DescNameOnly() << "\n";
  ofs << "name-only = " << (cfg_list_counters.name_only ? "true" : "false") << "\n\n";
}

// Save the [read-counter] section to an output stream
static void SaveTomlWithComments(std::ostream& ofs, const ReadCounterCfg& cfg_read_counter) {
  ofs << "[read-counter]\n";
  ofs << "\n# " << ReadCounterCfg::DescCounterName() << "\n";
  ofs << "counter-name = \"" << cfg_read_counter.counter_name << "\"\n\n";
}

// Write full TOML config file with comments describing each field (defaults or effective values)
static void SaveTomlWithComments(const std::string& path, const AppConfig& cfg) {
  std::ofstream ofs(path);
  if (!ofs) {
    throw std::runtime_error("Failed to open " + path + " for write");
  }

  ofs << "# astl-cli configuration\n\n";

  ofs << "# " << AppConfig::DescCommand() << "\n";
  ofs << "command = \"" << std::to_string(cfg.command) << "\"\n\n";

  // config_file is absent from the config file because you can only specify the config file from the command line

  ofs << "# " << AppConfig::DescAstlConfigFile() << " (optional)\n";
  if (cfg.astl_config_file.empty()) {
    ofs << "# astl_config_file = \"/path/to/astl_configuration.json\"\n\n";
  } else {
    ofs << "astl_config_file = " << cfg.astl_config_file << "\n\n";
  }

  ofs << "# " << AppConfig::DescVerbose() << "\n";
  ofs << "verbose = " << (cfg.verbose ? "true" : "false") << "\n\n";
  SaveTomlWithComments(ofs, cfg.list);
  SaveTomlWithComments(ofs, cfg.collect);
  SaveTomlWithComments(ofs, cfg.list_counters);
  SaveTomlWithComments(ofs, cfg.read_counter);
}

static void ApplyTomlConfigOverrides(const argparse::ArgumentParser& program, AppConfig& cfg) {
  if (program.is_used("--config-file")) {
    auto parse_path_result = astl::ExpandFilePath(program.get<std::string>("--config-file"));
    cfg.config_file        = parse_path_result.value_or(cfg.config_file);
    if (!parse_path_result) {
      std::cerr << parse_path_result.error() << "\n";
    }
  }
  if (auto tbl = LoadTomlIfExists(cfg.config_file)) {
    // optional top-level "command" and nested sections
    cfg.MergeFromToml(*tbl);
  }
}

/* Build command line argument parser and process given args */
void ParseCliArgs(int argc, char** argv, argparse::ArgumentParser& program, argparse::ArgumentParser& cmd_list,
                  argparse::ArgumentParser& cmd_collect, argparse::ArgumentParser& cmd_list_counters,
                  argparse::ArgumentParser& cmd_read_counter) {
  // Global options
  program.add_argument("-c", "--config-file").help(AppConfig::DescConfigFile()).default_value(std::string{""});
  // @todo(215) make astl app work on windows (HOME env var and path handling need work)
  std::string home_dir = astl::GetEnvVar("HOME");
  if (!home_dir.empty()) {
    home_dir.append("/");
  }
  program.add_argument("-k", "--astl-config-file")
      .help(AppConfig::DescAstlConfigFile())
      .default_value(home_dir + std::string{".astl_configuration.json"});
  program.add_argument("-v", "--verbose").help(AppConfig::DescVerbose()).flag();

  // -- save scaffolds
  program.add_argument("--save-config").help("Write configuration (input config file and CLI args) to TOML and exit.");

  // @todo(ASTL-247) - turn strings for commands and options and config file sections into constants.

  // list-metrics options (map to [list-metrics])
  cmd_list.add_argument("--units-include-list").help(ListMetricsCfg::DescUnitsIncludeList()).append();  // repeatable
  cmd_list.add_argument("--name-only").help(ListMetricsCfg::DescNameOnly()).flag();

  // collect options (map to [collect])
  cmd_collect.add_argument("-i", "--sampling-interval").help(CollectCfg::DescSamplingInterval()).scan<'i', int>();
  cmd_collect.add_argument("-d", "--duration").help(CollectCfg::DescDuration()).scan<'i', int>();
  cmd_collect.add_argument("-m", "--metric").help(CollectCfg::DescMetricsCli()).append();  // repeatable)
  cmd_collect.add_argument("-o", "--output-dir").help(CollectCfg::DescOutputDir());
  cmd_collect.add_argument("-p", "--plot-type").help(CollectCfg::DescPlotType()).default_value(std::string{"none"});
  // For workload, grab everything after "--" on CLI
  // e.g. astl-cli collect -i 100 -d 10000 -- someprog arg1 arg2
  // cmd_collect.add_argument("--").help("Workload program and arguments (all remaining arguments after '--' on
  // CLI)");
  cmd_collect.add_argument("--", "workload")
      .remaining()
      .help(std::string{CollectCfg::DescWorkload()} + " (all remaining arguments after '--' on CLI)");

  // list-counters options
  cmd_list_counters.add_argument("--name-only").help(ListCountersCfg::DescNameOnly()).flag();

  // read-counter options - counter_name is a positional parameter
  cmd_read_counter.add_argument("counter-name").help(ReadCounterCfg::DescCounterName()).remaining();

  // Register subcommands
  program.add_subparser(cmd_list);
  program.add_subparser(cmd_collect);
  program.add_subparser(cmd_list_counters);
  program.add_subparser(cmd_read_counter);

  // Parse CLI
  program.parse_args(argc, argv);
}

/**
 * @brief Modify the ListConfig settings based on what CLI args are given in 'program'.
 *        and its subcommand parser for list-metrics.
 * This takes precedence over config toml file.
 */
static void ApplyCliOverrides(const argparse::ArgumentParser& program, const argparse::ArgumentParser& cmd_list,
                              ListMetricsCfg& cfg_list) {
  if (!program.is_subcommand_used("list-metrics")) {
    return;
  }
  if (cmd_list.is_used("--units-include-list")) {
    auto units_as_str = cmd_list.get<std::vector<std::string>>("--units-include-list");
    cfg_list.units_include_list.clear();
    cfg_list.units_include_list.reserve(units_as_str.size());
    std::ranges::transform(units_as_str, std::back_inserter(cfg_list.units_include_list),
                           [](const std::string& unit) { return astl::ParseUnits(unit); });
  }
  if (cmd_list.is_used("--name-only")) {
    cfg_list.name_only = cmd_list.get<bool>("--name-only");
  }
}

/**
 * @brief Modify the CollectCfg settings based on what CLI args are given in 'program'.
 *        and its subcommand parser for collect
 * This takes precedence over config toml file.
 */
static void ApplyCliOverrides(const argparse::ArgumentParser& program, const argparse::ArgumentParser& cmd_collect,
                              CollectCfg& cfg_collect) {
  if (!program.is_subcommand_used("collect")) {
    return;
  }
  if (cmd_collect.is_used("--sampling-interval")) {
    cfg_collect.sampling_interval = std::chrono::milliseconds(cmd_collect.get<int>("--sampling-interval"));
  }
  if (cmd_collect.is_used("--duration")) {
    cfg_collect.duration = std::chrono::seconds(cmd_collect.get<int>("--duration"));
  }
  if (cmd_collect.is_used("--metric")) {
    cfg_collect.metrics = cmd_collect.get<std::vector<std::string>>("--metric");
  }
  if (cmd_collect.is_used("--output-dir")) {
    cfg_collect.output_dir = cmd_collect.get<std::string>("--output-dir");
  }
  if (cmd_collect.is_used("--plot-type")) {
    std::string ptype     = cmd_collect.get<std::string>("--plot-type");
    cfg_collect.plot_type = CollectCfg::FromString(ptype);
  }
  if (cmd_collect.is_used("workload")) {
    cfg_collect.workload = cmd_collect.get<std::vector<std::string>>("workload");
  }
}

/**
 * @brief Modify the ListCountersCfg settings based on what CLI args are given in 'program'.
 * This takes precedence over config toml file.
 */
static void ApplyCliOverrides(const argparse::ArgumentParser& program,
                              const argparse::ArgumentParser& cmd_list_counters, ListCountersCfg& cfg_list_counters) {
  if (!program.is_subcommand_used("list-counters")) {
    return;
  }
  if (cmd_list_counters.is_used("--name-only")) {
    cfg_list_counters.name_only = cmd_list_counters.get<bool>("--name-only");
  }
}

/**
 * @brief Modify the ReadCounterCfg settings based on what CLI args are given in 'program'.
 * This takes precedence over config toml file.
 */
static void ApplyCliOverrides(const argparse::ArgumentParser& program, const argparse::ArgumentParser& cmd_read_counter,
                              ReadCounterCfg& cfg_read_counter) {
  if (!program.is_subcommand_used("read-counter")) {
    return;
  }
  cfg_read_counter.counter_name = cmd_read_counter.get<std::string>("counter-name");
}

/**
 * @brief Modify the AppConfig settings based on what CLI args are given in 'program'.
 * This takes precedence over config toml file.
 */
static void ApplyCliOverrides(const argparse::ArgumentParser& program, const argparse::ArgumentParser& cmd_list,
                              const argparse::ArgumentParser& cmd_collect,
                              const argparse::ArgumentParser& cmd_list_counters,
                              const argparse::ArgumentParser& cmd_read_counter, AppConfig& cfg) {
  if (program.is_used("--astl-config-file")) {
    auto parse_path_result = astl::ExpandFilePath(program.get<std::string>("--astl-config-file"));
    cfg.astl_config_file   = parse_path_result.value_or(cfg.astl_config_file);
    if (!parse_path_result) {
      std::cerr << parse_path_result.error() << "\n";
    }
  }
  if (program.is_used("--verbose")) {
    cfg.verbose = program.get<bool>("--verbose");
  }
  // if verbose, set ASTL logging environment vars
  if (cfg.verbose) {
    if (astl::GetEnvVar("ASTL_LOG_LEVEL").empty()) {
      astl::SetEnvVar("ASTL_LOG_LEVEL", "DEBUG");
    }
    if (astl::GetEnvVar("ASTL_LOG_CONSOLE").empty()) {
      astl::SetEnvVar("ASTL_LOG_CONSOLE", "1");
    }
    std::cout << "Verbose ASTL log output enabled\n";
  }
  // capture CLI arguments for subcommands in cfg.list and cfg.collect
  ApplyCliOverrides(program, cmd_list, cfg.list);
  ApplyCliOverrides(program, cmd_collect, cfg.collect);
  ApplyCliOverrides(program, cmd_list_counters, cfg.list_counters);
  ApplyCliOverrides(program, cmd_read_counter, cfg.read_counter);
}

static auto InvokeSubcommand(const argparse::ArgumentParser& program, const AppConfig& cfg) -> int {
  using SubcommandT = int(const AppConfig&);

  const std::unordered_map<std::string, SubcommandT*> cli_subcommand_map = {
      {"collect",       &Collect     },
      {"list-metrics",  &ListMetrics },
      {"list-counters", &ListCounters},
      {"read-counter",  &ReadCounter },
  };
  const std::unordered_map<AppConfig::Command, SubcommandT*> toml_subcommand_map = {
      {AppConfig::Command::COLLECT,       &Collect     },
      {AppConfig::Command::LIST_METRICS,  &ListMetrics },
      {AppConfig::Command::LIST_COUNTERS, &ListCounters},
      {AppConfig::Command::READ_COUNTER,  &ReadCounter },
  };

  // first priority is to check the command line for a subcommand
  for (const auto& [name, func] : cli_subcommand_map) {
    if (program.is_subcommand_used(name)) {
      return func(cfg);
    }
  }

  // if no subcommand on CLI, use the command from the cfg file.
  if (toml_subcommand_map.contains(cfg.command)) {
    auto func = toml_subcommand_map.at(cfg.command);
    return func(cfg);
  }
  // expected unreachable - the toml default value for command should be list-metrics
  std::cerr << "No subcommand given. Use --help for usage information.\n";
  return 1;
}

// ------------------------------- main --------------------------------------
int main(int argc, char** argv) {
  try {
    argparse::ArgumentParser program{"astl-cli", "1.0"};
    argparse::ArgumentParser cmd_list{"list-metrics"};
    argparse::ArgumentParser cmd_collect{"collect"};
    argparse::ArgumentParser cmd_list_counters{"list-counters"};
    argparse::ArgumentParser cmd_read_counter{"read-counter"};

    ParseCliArgs(argc, argv, program, cmd_list, cmd_collect, cmd_list_counters, cmd_read_counter);

    AppConfig cfg;  // start with default configuration

    // Load TOML config if provided, merge into cfg
    ApplyTomlConfigOverrides(program, cfg);

    // CLI overrides (highest precedence), some per subcommand
    ApplyCliOverrides(program, cmd_list, cmd_collect, cmd_list_counters, cmd_read_counter, cfg);

    // Save configuration to toml file and exit
    if (program.is_used("--save-config")) {
      auto path = program.get<std::string>("--save-config");
      SaveTomlWithComments(path, cfg);
      std::cout << "Wrote config to: " << path << "\n";
      return 0;
    }

    return InvokeSubcommand(program, cfg);

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}