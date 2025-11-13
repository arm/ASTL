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

#include "list_metrics.hpp"

#include <expected>
#include <iostream>
#include <ranges>
#include <tabulate/table.hpp>  // https://github.com/p-ranav/tabulate

#include "app_config.hpp"
#include "astl/astl_telemetry.h"
#include "astl_metrics.hpp"
#include "utils.hpp"

static void PrintCommandConfig(const AppConfig& cfg) {
  std::cout << "[list-metrics]\n";
  std::cout << "  units-include-list : ";
  if (cfg.list.units_include_list.empty()) {
    std::cout << "(none)\n";
  } else {
    bool first = true;
    for (auto const& units_include : cfg.list.units_include_list) {
      std::cout << (first ? "" : ", ") << std::to_string(units_include);
      first = false;
    }
    std::cout << "\n";
  }
}

bool IsIncludedMetric(const Metric& metric, const AppConfig& cfg) {
  if (!cfg.list.units_include_list.empty()) {
    if (std::find(cfg.list.units_include_list.begin(), cfg.list.units_include_list.end(), metric.properties._units) ==
        cfg.list.units_include_list.end()) {
      return false;
    }
  }
  return true;
}

int ListMetrics(const AppConfig& cfg) {
  if (cfg.verbose) {
    PrintCommandConfig(cfg);
  }
  auto all_metrics = DetectMetrics(cfg);
  if (!all_metrics) {
    std::cerr << "Error detecting metrics: " << astlStatusString(all_metrics.error()) << "\n";
    return -1;
  }

  // apply filters on the metrics
  auto filtered_metrics =
      std::views::filter(*all_metrics, [&](const Metric& metric) { return IsIncludedMetric(metric, cfg); });

  if (cfg.list.name_only) {
    for (const auto& metric : filtered_metrics) {
      std::cout << metric.properties._name << "\n";
    }
    return 0;
  }

  // Using tabulate to create a table
  tabulate::Table table;
  table.add_row({"Metric Name", "Description", "Units", "Metric Type"});

  for (auto& metric : filtered_metrics) {
    // Get target names for this metric
    std::string target_names;
    bool        first = true;
    for (auto const& [target_handle, target_name] : metric.target_handles_and_names) {
      target_names += (first ? "" : ", ") + target_name;
      first = false;
    }
    table.add_row({metric.properties._name, metric.properties._description, UnitsToString(metric.properties._units),
                   MetricTypeToString(metric.properties._metric_type)});
  }
  // set border style
  table.format()
      .corner_top_left("┌")
      .corner_top_right("┐")
      .corner_bottom_left("└")
      .corner_bottom_right("┘")
      .border_top("─")
      .border_bottom("─")
      .border_left("│")
      .border_right("│");

  // set column header fonts
  std::for_each(table[0].begin(), table[0].end(), [](tabulate::Cell& cell) {
    cell.format()
        .font_align(tabulate::FontAlign::center)
        .font_style({tabulate::FontStyle::bold})
        .corner_bottom_left("├");
  });

  std::cout << table << std::endl;

  return 0;
}