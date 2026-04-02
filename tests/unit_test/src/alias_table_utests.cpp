// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "../../../tools/atx/alias_table.hpp"
#include "../../test_includes.hpp"

namespace {

auto WriteTextFile(const std::filesystem::path& path, std::string_view contents) -> void {
  std::error_code err;
  std::filesystem::create_directories(path.parent_path(), err);
  REQUIRE_FALSE(err);

  std::ofstream output(path, std::ios::out | std::ios::trunc);
  REQUIRE(output.is_open());
  output << contents;
  REQUIRE(output.good());
}

}  // namespace

TEST_CASE("LoadAliasTable reads exact and family aliases", "[ATX][AliasTable]") {
  const auto temp_root =
      std::filesystem::temp_directory_path() /
      ("astl_alias_table_test_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  const auto alias_table_path = temp_root / "alias_table.json";
  WriteTextFile(alias_table_path, R"({
  "exact_aliases": {
    "soc-temp": {
      "metric_name": "apm_xgene-isa-0000_SoC_Temperature"
    }
  },
  "family_aliases": [
    {
      "metric_name_prefix": "nvme-pci-",
      "alias_prefix": "nvme",
      "members": {
        "Composite": "temp"
      }
    }
  ]
})");

  auto document_or_error = LoadAliasTable(alias_table_path);
  REQUIRE(document_or_error.has_value());
  REQUIRE(document_or_error->exact_aliases.contains("soc-temp"));
  REQUIRE(document_or_error->family_aliases.size() == 1);
  CHECK(document_or_error->family_aliases[0].metric_name_prefix == "nvme-pci-");
  CHECK(document_or_error->family_aliases[0].alias_prefix == "nvme");
  CHECK(document_or_error->family_aliases[0].members.at("Composite") == "temp");

  std::error_code remove_err;
  std::filesystem::remove_all(temp_root, remove_err);
  REQUIRE_FALSE(remove_err);
}

TEST_CASE("ResolveAliases assigns family aliases by first-seen chip order", "[ATX][AliasTable]") {
  const AliasTableDocument document{
      .exact_aliases =
          {
                          {"soc-temp", AliasTableEntry{"apm_xgene-isa-0000_SoC_Temperature"}},
                          {"cpu-fan", AliasTableEntry{"system76_thelio_io-hid-3-5_CPU_Fan"}},
                          },
      .family_aliases =
          {
                          AliasFamilyEntry{
                  .metric_name_prefix = "nvme-pci-",
                  .alias_prefix       = "nvme",
                  .members =
                      {
                          {"Composite", "temp"},
                          {"Sensor_2", "temp2"},
                          {"Composite_alarm", "temp-alarm"},
                      },
              },                                                                   AliasFamilyEntry{
                  .metric_name_prefix = "bnxt_en-pci-",
                  .alias_prefix       = "nic",
                  .members            = {{"temp1", "temp"}},
              }, },
  };

  const std::vector<std::string> metric_names{
      "apm_xgene-isa-0000_SoC_Temperature",
      "nvme-pci-40100_Composite",
      "nvme-pci-40100_Sensor_2",
      "bnxt_en-pci-20300_temp1",
      "nvme-pci-20100_Composite",
      "nvme-pci-20100_Composite_alarm",
      "bnxt_en-pci-0800_temp1",
      "system76_thelio_io-hid-3-5_CPU_Fan",
  };

  const auto aliases_by_metric_name = ResolveAliases(document, metric_names);
  REQUIRE(aliases_by_metric_name.at("apm_xgene-isa-0000_SoC_Temperature") == "soc-temp");
  REQUIRE(aliases_by_metric_name.at("system76_thelio_io-hid-3-5_CPU_Fan") == "cpu-fan");
  REQUIRE(aliases_by_metric_name.at("nvme-pci-40100_Composite") == "nvme1-temp");
  REQUIRE(aliases_by_metric_name.at("nvme-pci-40100_Sensor_2") == "nvme1-temp2");
  REQUIRE(aliases_by_metric_name.at("nvme-pci-20100_Composite") == "nvme2-temp");
  REQUIRE(aliases_by_metric_name.at("nvme-pci-20100_Composite_alarm") == "nvme2-temp-alarm");
  REQUIRE(aliases_by_metric_name.at("bnxt_en-pci-20300_temp1") == "nic1-temp");
  REQUIRE(aliases_by_metric_name.at("bnxt_en-pci-0800_temp1") == "nic2-temp");
}
