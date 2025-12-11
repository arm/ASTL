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

#include "list_counters.hpp"

#include <expected>
#include <iostream>
#include <ranges>
#include <tabulate/table.hpp>  // https://github.com/p-ranav/tabulate

#include "app_config.hpp"
#include "astl/astl_telemetry.h"
#include "astl_counters.hpp"
#include "utils.hpp"

static void PrintCommandConfig(const AppConfig& cfg) {
  std::cout << "[list-counters]\n";
  std::cout << "  name-only : " << (cfg.list_counters.name_only ? "true" : "false") << "\n";
}

int ListCounters(const AppConfig& cfg) {
  if (cfg.verbose) {
    PrintCommandConfig(cfg);
  }
  auto all_counters = DetectCounters(cfg);
  if (!all_counters) {
    std::cerr << "Error detecting counters: " << astlStatusString(all_counters.error()) << "\n";
    return -1;
  }

  if (cfg.list_counters.name_only) {
    for (const auto& counter : *all_counters) {
      std::cout << counter.properties._name << "\n";
    }
    return 0;
  }

  // Using tabulate to create a table
  tabulate::Table table;
  table.add_row({"Counter Name", "Description", "Units"});

  for (auto& counter : *all_counters) {
    table.add_row(
        {counter.properties._name, counter.properties._description, UnitsToString(counter.properties._units)});
  }
  SetTabulateTableStyle(table);
  std::cout << table << std::endl;

  return 0;
}