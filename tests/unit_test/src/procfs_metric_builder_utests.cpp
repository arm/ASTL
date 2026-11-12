// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
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
#include "astl_cxx_compat.hpp"
#include "astl_utils.hpp"
#include "common/capabilities.hpp"
#include "config/astl_configuration.hpp"
#include "config/json_file_utils.hpp"
#include "config/metric_json_declaration.hpp"
#include "metric/metric_builder.hpp"
#include "metric/metric_manager.hpp"
#include "procfs/procfs_metric_builder.hpp"
#include "procfs/procfs_metric_builder_helpers.hpp"
#include "procfs/procfs_metric_builder_parsing_helpers.hpp"
#include "procfs/procfs_read_operation.hpp"
#include "procfs/procfs_target.hpp"
#include "procfs/procfs_utils.hpp"

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

TEST_CASE("Shipped procfs metrics work without platform-specific configuration", "[procfs_metric_builder]") {
  const fs::path config_dir  = fs::temp_directory_path() / "astl_generic_procfs_config";
  const fs::path procfs_root = fs::temp_directory_path() / "astl_generic_procfs_root";
  TempFileGuard  config_guard(config_dir);
  TempFileGuard  procfs_guard(procfs_root);
  const fs::path source_dir{ASTL_TEST_SOURCE_DIR};
  fs::create_directories(config_dir / "metrics" / "procfs");
  fs::create_directories(config_dir / "groups");
  fs::copy_file(source_dir / "config/metrics/procfs/metrics.json", config_dir / "metrics/procfs/metrics.json");
  fs::copy_file(source_dir / "config/groups/metric_groups.json", config_dir / "groups/metric_groups.json");
  WriteTextFile(procfs_root / "meminfo", "MemTotal: 1024 kB\nMemAvailable: 256 kB\n");
  WriteTextFile(procfs_root / "stat", "cpu 1 2 3 4 5 6 7 8 9 10\ncpu0 1 2 3 4 5 6 7 8 9 10\n");
  EnvVarGuard config_dir_guard(astl::EnvVar::ASTL_CONFIG_DIR, config_dir.string());
  auto        configuration = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration.has_value());
  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(std::make_unique<astl::ProcfsTarget>("procfs", "generic Android or Linux", procfs_root));
  auto manager = astl::BuildMetricManager(targets, *configuration, astl::compat::nullopt);
  REQUIRE(manager.has_value());
  CHECK(CollectMetricNames(**manager, targets.front().get()) ==
        std::vector<std::string>{"cpu.0.iowait", "cpu.0.utilization", "cpu.total.iowait", "cpu.total.utilization",
                                 "meminfo.MemAvailable", "memory.total", "memory.used", "memory.utilization",
                                 "system.current_processes", "system.current_threads"});
}

TEST_CASE("RegisterProcfsMetrics loads config-defined procfs metrics", "[procfs_metric_builder]") {
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
    "memory.total": {
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
    "memory.used": {
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
    "memory.utilization": {
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
    "cpu.{cpu_index}.utilization": {
      "description": "Procfs /proc/stat {label} busy percent over sample interval",
      "unit": "percent",
      "value_type": "float64",
      "metric_type": "value",
      "identifier": "unknown",
      "metric_groups": ["cpu"],
      "collection": {
        "protocol": "procfs",
        "field_type": "cpu_utilization",
        "relative_path": "stat",
        "expand": {
          "relative_path": "stat",
          "match_pattern": "^cpu[0-9]*$",
          "label_token_index": 0
        }
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
  REQUIRE(metric_names == std::vector<std::string>{"cpu.0.utilization", "cpu.1.utilization", "cpu.total.utilization",
                                                   "memory.total", "memory.used", "memory.utilization"});

  auto metrics = metric_manager.GetAvailableMetrics(target.get());
  REQUIRE(metrics.has_value());
  for (const auto* const metric_handle : *metrics) {
    astl_metric_props_t props{};
    REQUIRE(metric_manager.GetProperties(metric_handle, &props) == ASTL_STATUS_SUCCESS);
    const std::string_view metric_name{props.name == nullptr ? "" : props.name};
    if (!metric_name.starts_with("stat.")) {
      continue;
    }

    auto operations = metric_manager.GetRequiredOperations({&metric_handle, 1}, target.get());
    REQUIRE(operations.has_value());
    REQUIRE(operations->operationsOnSample.size() == 1);

    const auto* read_operation =
        dynamic_cast<const astl::ProcfsReadOperation*>(operations->operationsOnSample.front().get());
    REQUIRE(read_operation != nullptr);
    const auto* field = std::get_if<astl::procfs::CpuUtilizationField>(&read_operation->field_descriptor);
    REQUIRE(field != nullptr);
    REQUIRE(field->relative_path == "stat");
    REQUIRE(metric_name == "stat." + field->line_prefix + ".utilization");
  }

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

TEST_CASE("Default procfs discovery exposes optional system sources with identities", "[procfs_metric_builder]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_default_system_discovery";
  TempFileGuard guard{root};
  WriteTextFile(root / "diskstats", "8 0 sda 10 0 20 0 30 0 40 0 0 0 0\n8 1 sda1 1 0 2 0 3 0 4 0 0 0 0\n");
  WriteTextFile(root / "net/dev",
                "header\nheader\n lo: 10 0 0 0 0 0 0 0 20 0 0 0 0 0 0 0\n eth0:30 0 0 0 0 0 0 0 40 0 0 0 0 0 0 0\n");
  WriteTextFile(root / "interrupts", " CPU0 CPU2\n 1: 10 20 controller device\n");
  WriteTextFile(root / "softirqs", " CPU0 CPU2\n TIMER: 30 40\n");
  WriteTextFile(root / "vmstat", "pgfault 100\npgmajfault 3\n");
  WriteTextFile(
      root / "stat",
      "cpu 1 0 1 10 2 0 0 0\ncpu0 1 0 1 10 2 0 0 0\nctxt 100\nintr 30\nsoftirq 70\nprocs_running 2\nprocs_blocked 1\n");
  WriteTextFile(root / "loadavg", "0.25 1.50 2.75 2/10 123\n");
  WriteTextFile(root / "meminfo", "MemTotal: 100 kB\nMemAvailable: 50 kB\nSwapTotal: 10 kB\nSwapFree: 4 kB\n");
  WriteTextFile(root / "1/status", "Threads: 2\n");
  astl::FileInterface files{root};
  astl::ProcfsTarget  target{"procfs", "fixture", root};
  const auto          config_path =
      fs::path{__FILE__}.parent_path().parent_path().parent_path().parent_path() / "config/metrics/procfs/metrics.json";
  auto declarations = astl::config::TryParseJsonFile<astl::metrics::spec::MetricsDeclaration>(config_path);
  REQUIRE(declarations);
  std::vector<std::string> names;
  for (const auto& [name, declaration] : declarations->metrics) {
    auto configs = astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs(name, declaration, &target, files);
    REQUIRE(configs);
    for (const auto& config : *configs) {
      names.push_back(config->Name());
    }
  }
  for (const auto& name : {"disk.sda.read_operations_per_second",
                           "disk.sda1.write_bytes_per_second",
                           "network.eth0.rx_bytes_per_second",
                           "network.lo.tx_bytes",
                           "vm.page_faults",
                           "vm.major_page_faults",
                           "cpu.2.interrupts_per_second",
                           "cpu.0.soft_interrupts",
                           "system.current_processes",
                           "system.current_threads",
                           "cpu.total.iowait",
                           "swap.used",
                           "swap.occupancy",
                           "scheduler.context_switches_per_second",
                           "swap.total",
                           "scheduler.load_average_1m",
                           "scheduler.load_average_5m",
                           "scheduler.load_average_15m",
                           "scheduler.runnable_tasks",
                           "scheduler.blocked_tasks",
                           "vm.page_faults_per_second",
                           "vm.major_page_faults_per_second"}) {
    INFO(name);
    CHECK(std::ranges::find(names, name) != names.end());
  }
  auto raw = astl::procfs::DiscoverCounterDescriptors(files);
  REQUIRE(raw);
  CHECK(
      std::ranges::any_of(*raw, [](const auto& descriptor) { return descriptor.metric_name == "vmstat.pgmajfault"; }));
  CHECK(std::ranges::any_of(
      *raw, [](const auto& descriptor) { return descriptor.metric_name == "diskstats.sda.read_bytes"; }));
  fs::remove(root / "vmstat");
  auto missing                              = declarations->metrics.at("vm.page_faults");
  missing.collection.raw_json["field_type"] = "SYSTEM";
  auto unavailable =
      astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs("vm.page_faults", missing, &target, files);
  REQUIRE(unavailable);
  CHECK(unavailable->empty());
  WriteTextFile(root / "stat", "cpu 1 2 3 4\n");
  auto iowait                              = declarations->metrics.at("cpu.{cpu_index}.iowait");
  iowait.collection.raw_json["field_type"] = "CPU_IOWAIT";
  auto missing_iowait =
      astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs("cpu.{cpu_index}.iowait", iowait, &target, files);
  REQUIRE(missing_iowait);
  CHECK(missing_iowait->empty());
  // A present meminfo file without swap fields cannot support swap gauges.
  WriteTextFile(root / "meminfo", "MemTotal: 100 kB\nMemAvailable: 50 kB\n");
  for (const auto* name : {"swap.total", "swap.used", "swap.occupancy"}) {
    auto unsupported = astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs(
        name, declarations->metrics.at(name), &target, files);
    REQUIRE(unsupported);
    CHECK(unsupported->empty());
  }
  WriteTextFile(root / "softirqs", " CPU0 CPU2\n TIMER: 1 2 3\n");
  auto invalid_softirqs = astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs(
      "cpu.{cpu_index}.soft_interrupts_per_second",
      declarations->metrics.at("cpu.{cpu_index}.soft_interrupts_per_second"), &target, files);
  REQUIRE(invalid_softirqs);
  CHECK(invalid_softirqs->empty());
  raw = astl::procfs::DiscoverCounterDescriptors(files);
  REQUIRE(raw);
  CHECK_FALSE(std::ranges::any_of(
      *raw, [](const auto& descriptor) { return descriptor.metric_name.starts_with("softirqs."); }));
}

TEST_CASE("Task gauge discovery survives incomplete task scans", "[procfs_metric_builder]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_task_discovery_churn";
  TempFileGuard guard{root};
  fs::create_directories(root / "123");
  astl::FileInterface files{root};
  astl::ProcfsTarget  target{"procfs", "fixture", root};
  const auto          config_path = fs::path{ASTL_TEST_SOURCE_DIR} / "config/metrics/procfs/metrics.json";
  auto declarations = astl::config::TryParseJsonFile<astl::metrics::spec::MetricsDeclaration>(config_path);
  REQUIRE(declarations);
  auto discover = [&](const std::string& name) {
    auto configs = astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs(name, declarations->metrics.at(name),
                                                                                  &target, files);
    REQUIRE(configs);
    auto raw = astl::procfs::DiscoverCounterDescriptors(files);
    REQUIRE(raw);
    const auto raw_count =
        std::ranges::count_if(*raw, [&](const auto& descriptor) { return descriptor.metric_name == name; });
    CHECK(static_cast<size_t>(raw_count) == configs->size());
    return configs->size();
  };
  const astl::procfs::SystemField threads{".", "", "threads"};
  // A vanished/unreadable status must fail collection without hiding either gauge.
  CHECK_FALSE(astl::procfs::ReadField(files, threads));
  CHECK(discover("system.current_threads") == 1);
  CHECK(discover("system.current_processes") == 1);
  WriteTextFile(root / "123/status", "Threads: invalid\n");
  CHECK_FALSE(astl::procfs::ReadField(files, threads));
  CHECK(discover("system.current_threads") == 1);
  WriteTextFile(root / "123/status", "Threads: 2\n");
  auto recovered = astl::procfs::ReadField(files, threads);
  REQUIRE(recovered);
  CHECK(std::get<uint64_t>(recovered->value) == 2);
  fs::remove_all(root);
  CHECK(discover("system.current_threads") == 0);
  CHECK(discover("system.current_processes") == 0);
}

TEST_CASE("Optional scheduler and VM metrics require their individual source fields", "[procfs_metric_builder]") {
  const auto    root = fs::temp_directory_path() / "astl_procfs_optional_scheduler";
  TempFileGuard guard{root};
  fs::create_directories(root);
  astl::FileInterface files{root};
  astl::ProcfsTarget  target{"procfs", "fixture", root};
  auto                declarations = astl::config::TryParseJsonFile<astl::metrics::spec::MetricsDeclaration>(
      fs::path{ASTL_TEST_SOURCE_DIR} / "config/metrics/procfs/metrics.json");
  REQUIRE(declarations);
  for (const auto* name : {"scheduler.runnable_tasks", "scheduler.blocked_tasks", "vm.page_faults_per_second",
                           "vm.major_page_faults_per_second", "swap.total", "scheduler.load_average_1m",
                           "scheduler.load_average_5m", "scheduler.load_average_15m"}) {
    auto configs = astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs(name, declarations->metrics.at(name),
                                                                                  &target, files);
    REQUIRE(configs);
    CHECK(configs->empty());
  }
  WriteTextFile(root / "loadavg", "0.25 1.50 2.75 2/10 123\n");
  const std::unordered_map<std::string, double> loads{
      {"scheduler.load_average_1m",  0.25},
      {"scheduler.load_average_5m",  1.50},
      {"scheduler.load_average_15m", 2.75}
  };
  for (const auto& [name, expected] : loads) {
    auto settings =
        astl::metrics::spec::ParseProcfsMetricJsonCollectionSettings(declarations->metrics.at(name).collection);
    REQUIRE(settings);
    auto descriptor =
        astl::procfs_metric_builder_helpers::detail::ParseProcfsFieldDescriptor(*settings, astl::compat::nullopt);
    REQUIRE(descriptor);
    auto value = astl::procfs::ReadField(files, *descriptor);
    REQUIRE(value);
    CHECK(std::get<double>(value->value) == Catch::Approx(expected));
  }
  WriteTextFile(root / "loadavg", "0.25 invalid 2.75 2/10 123\n");
  for (const auto* name : {"scheduler.load_average_1m", "scheduler.load_average_5m", "scheduler.load_average_15m"}) {
    auto configs = astl::procfs_metric_builder_helpers::CreateProcfsMetricConfigs(name, declarations->metrics.at(name),
                                                                                  &target, files);
    REQUIRE(configs);
    CHECK(configs->size() == (std::string_view{name} == "scheduler.load_average_5m" ? 0 : 1));
  }
}
