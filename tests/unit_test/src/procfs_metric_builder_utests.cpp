// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "astl_utils.hpp"
#include "common/capabilities.hpp"
#include "config/astl_configuration.hpp"
#include "metric/metric_manager.hpp"
#include "metric/procfs_metric_builder.hpp"
#include "topology/procfs_target.hpp"

namespace fs = std::filesystem;

namespace {

void WriteTextFile(const fs::path& path, std::string_view contents) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  REQUIRE(!ec);

  std::ofstream out(path, std::ios::out | std::ios::trunc);
  REQUIRE(out.good());
  out << contents;
  REQUIRE(out.good());
}

void CreateConfigTree(const fs::path& config_dir) {
  std::error_code ec;
  fs::create_directories(config_dir / "metrics" / "procfs", ec);
  REQUIRE(!ec);
  fs::create_directories(config_dir / "groups", ec);
  REQUIRE(!ec);
  fs::create_directories(config_dir / "scmi" / "public", ec);
  REQUIRE(!ec);
}

auto CollectMetricNames(astl::IMetricManager& metric_manager, const astl::ITarget* target) -> std::vector<std::string> {
  auto metrics = metric_manager.GetAvailableMetrics(target);
  REQUIRE(metrics.has_value());

  std::vector<std::string> metric_names;
  metric_names.reserve(metrics->size());
  for (const auto* const metric_handle : *metrics) {
    astl_metric_props_t props{};
    REQUIRE(metric_manager.GetProperties(metric_handle, &props) == ASTL_STATUS_SUCCESS);
    metric_names.emplace_back(props.name == nullptr ? "" : props.name);
  }
  std::ranges::sort(metric_names);
  return metric_names;
}

}  // namespace

TEST_CASE("RegisterProcfsMetrics loads config-defined procfs composite metrics", "[procfs_metric_builder]") {
  const fs::path config_dir  = fs::temp_directory_path() / "astl_procfs_metric_builder_config";
  const fs::path procfs_root = fs::temp_directory_path() / "astl_procfs_metric_builder_root";
  TempFileGuard  config_guard(config_dir);
  TempFileGuard  procfs_guard(procfs_root);

  CreateConfigTree(config_dir);
  WriteTextFile(config_dir / "groups" / "metric_groups.json",
                R"json({
  "metric_groups": {
    "cpu": {
      "description": "Unit test CPU metrics"
    },
    "memory": {
      "description": "Unit test memory metrics"
    }
  }
})json");
  WriteTextFile(config_dir / "metrics" / "procfs" / "metrics.json",
                R"json({
  "document": {
    "confidential": false
  },
  "metrics": {
    "meminfo.MemTotal": {
      "description": "Procfs /proc/meminfo field MemTotal",
      "unit": "bytes",
      "value_type": "uint64",
      "metric_type": "value",
      "identifier": "unknown",
      "metric_groups": ["memory"],
      "formula": "value * 1024",
      "collection": {
        "protocol": "procfs",
        "field_type": "key_value",
        "relative_path": "meminfo",
        "field_name": "MemTotal",
        "raw_value_type": "uint64"
      }
    },
    "meminfo.MemUsed": {
      "description": "Procfs /proc/meminfo derived used memory (MemTotal - MemAvailable)",
      "unit": "bytes",
      "value_type": "uint64",
      "metric_type": "value",
      "identifier": "unknown",
      "metric_groups": ["memory"],
      "formula": "max(mem_total - mem_available, 0) * 1024",
      "collection": {
        "protocol": "procfs",
        "inputs": [
          {
            "name": "mem_total",
            "field_type": "key_value",
            "relative_path": "meminfo",
            "field_name": "MemTotal",
            "raw_value_type": "uint64"
          },
          {
            "name": "mem_available",
            "field_type": "key_value",
            "relative_path": "meminfo",
            "field_name": "MemAvailable",
            "raw_value_type": "uint64"
          }
        ]
      }
    },
    "meminfo.utilization": {
      "description": "Procfs /proc/meminfo derived used memory percent",
      "unit": "percent",
      "value_type": "float64",
      "metric_type": "value",
      "identifier": "unknown",
      "metric_groups": ["memory"],
      "formula": "if(mem_total == 0, 0, max(mem_total - mem_available, 0) * 100 / mem_total)",
      "collection": {
        "protocol": "procfs",
        "inputs": [
          {
            "name": "mem_total",
            "field_type": "key_value",
            "relative_path": "meminfo",
            "field_name": "MemTotal",
            "raw_value_type": "uint64"
          },
          {
            "name": "mem_available",
            "field_type": "key_value",
            "relative_path": "meminfo",
            "field_name": "MemAvailable",
            "raw_value_type": "uint64"
          }
        ]
      }
    },
    "stat.{label}.utilization": {
      "description": "Procfs /proc/stat {label} busy percent over sample interval",
      "unit": "percent",
      "value_type": "float64",
      "metric_type": "value",
      "identifier": "unknown",
      "metric_groups": ["cpu"],
      "formula": "clamp(if(delta_total == 0, 0, max(delta_total - delta_idle, 0) * 100 / delta_total), 0, 100)",
      "collection": {
        "protocol": "procfs",
        "expand": {
          "relative_path": "stat",
          "match_pattern": "^cpu[0-9]*$",
          "label_token_index": 0
        },
        "requires_previous": true,
        "inputs": [
          {
            "name": "total",
            "field_type": "token_sum",
            "relative_path": "stat",
            "line_prefix": "{label}",
            "token_start_index": 1,
            "token_end_index": 8
          },
          {
            "name": "idle",
            "field_type": "token_sum",
            "relative_path": "stat",
            "line_prefix": "{label}",
            "token_start_index": 4,
            "token_end_index": 5
          }
        ]
      }
    }
  }
})json");

  WriteTextFile(procfs_root / "meminfo", "MemTotal: 1024 kB\nMemAvailable: 256 kB\n");
  WriteTextFile(procfs_root / "stat",
                "cpu  1 2 3 4 5 6 7 8 9 10\ncpu0 1 1 1 1 1 1 1 1 1 1\ncpu1 2 2 2 2 2 2 2 2 2 2\n");

  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, config_dir.string());
  auto        configuration = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration.has_value());

  auto target = std::make_unique<astl::ProcfsTarget>("procfs", "unit-test procfs target", procfs_root);
  std::unordered_map<astl::CollectorType, std::vector<const astl::ITarget*>> collector_type_to_targets_map{
      {astl::CollectorType::PROCFS, {target.get()}}
  };

  astl::Capabilities capabilities{{astl::CollectorCapability{astl::CollectorType::PROCFS}}, {astl::SystemCapability{}}};
  astl::MetricManager::MetricGroupDescriptionMap metric_group_descriptions{
      {"cpu",    "Unit test CPU metrics"   },
      {"memory", "Unit test memory metrics"}
  };
  astl::MetricManager metric_manager{capabilities, std::move(metric_group_descriptions)};

  REQUIRE(astl::RegisterProcfsMetrics(*configuration, collector_type_to_targets_map, &metric_manager) ==
          ASTL_STATUS_SUCCESS);

  const auto metric_names = CollectMetricNames(metric_manager, target.get());
  REQUIRE(metric_names == std::vector<std::string>{"meminfo.MemTotal", "meminfo.MemUsed", "meminfo.utilization",
                                                   "stat.cpu.utilization", "stat.cpu0.utilization",
                                                   "stat.cpu1.utilization"});

  auto groups_or_error = metric_manager.GetMetricGroups(target.get());
  REQUIRE(groups_or_error.has_value());
  REQUIRE(groups_or_error->size() == 2);

  std::vector<std::string> group_names;
  group_names.reserve(groups_or_error->size());
  for (const auto* const group_handle : *groups_or_error) {
    astl_metric_group_props_t group_props{};
    group_props.size = sizeof(astl_metric_group_props_t);
    REQUIRE(metric_manager.GetMetricGroupProperties(group_handle, &group_props) == ASTL_STATUS_SUCCESS);
    group_names.emplace_back(group_props.name);
  }
  std::ranges::sort(group_names);
  REQUIRE(group_names == std::vector<std::string>{"cpu", "memory"});
}
