// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
#include "astl/astl_errors.h"
#include "astl_internal_status.hpp"
#include "common/scmi/uuid.hpp"
#include "config/config_lookup_loader.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/scmi_metric_json_declaration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"

using json = nlohmann::json;

namespace {

auto MakeTempDir(std::string_view prefix) -> std::filesystem::path {
  const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto       path = std::filesystem::temp_directory_path() / (std::string{prefix} + std::to_string(unique_suffix));
  std::filesystem::create_directories(path);
  return path;
}

void WriteTextFile(const std::filesystem::path& path, std::string_view contents) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  REQUIRE(!ec);

  std::ofstream out(path, std::ios::out | std::ios::trunc);
  REQUIRE(out.good());
  out << contents;
  REQUIRE(out.good());
}

}  // namespace

TEST_CASE("ScmiSpecification::NormalizeUUID", "[ConfigManager]") {
  SECTION("Typical lowercase uuid") {
    std::string raw_uuid = "550e8400-e29b-41d4-a716-446655440000";
    auto        uuid     = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    REQUIRE(uuid.normalized_value == "550e8400e29b41d4a716446655440000");
    REQUIRE(uuid.num_significant_bytes == 16);
  }
  SECTION("Typical uppercase uuid") {
    std::string raw_uuid = "CAFEBABE-41D4-12A716-446655440000/14";
    auto        uuid     = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    REQUIRE(uuid.normalized_value == "cafebabe41d412a716446655440000");
    REQUIRE(uuid.num_significant_bytes == 14);
  }
  SECTION("Strip 0x prefix uuid") {
    std::string raw_uuid = "0xCAFEBABE-41D4-12A716-446655440000/14";
    auto        uuid     = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    REQUIRE(uuid.normalized_value == "cafebabe41d412a716446655440000");
  }
  SECTION("Compare full UUIDs") {
    std::string raw_uuid = "550e8400-e29b-41d4-a716-446655441234";
    auto        uuid_a   = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    auto        uuid_b   = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    REQUIRE(uuid_a == uuid_b);
  }
  SECTION("Compare one short UUID") {
    std::string raw_uuid        = "CAFEBABE-41D4-12AC-A716-446655440000/14";
    std::string raw_target_uuid = "CAFEBABE-41D4-12AC-A716-446655441234";
    auto        uuid_matcher    = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    auto        target_uuid     = astl::scmi::spec::GetNormalizedUuid(raw_target_uuid).value();
    REQUIRE(uuid_matcher == target_uuid);
  }
  SECTION("Not quite a match, lower bytes") {
    std::string raw_uuid        = "CAFEBABE-41D4-12A716-446655441200/15";
    std::string raw_target_uuid = "DAFEBABE-41D4-12A716-446655441300";
    auto        uuid_matcher    = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    auto        target_uuid     = astl::scmi::spec::GetNormalizedUuid(raw_target_uuid).value();
    REQUIRE(uuid_matcher != target_uuid);
  }
  SECTION("Not quite a match, upper bytes") {
    std::string raw_uuid        = "CAFEBABE-41D4-12A716-446655440000/14";
    std::string raw_target_uuid = "DAFEBABE-41D4-12A716-446655440000";
    auto        uuid_matcher    = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    auto        target_uuid     = astl::scmi::spec::GetNormalizedUuid(raw_target_uuid).value();
    REQUIRE(uuid_matcher != target_uuid);
  }
  SECTION("Empty string should fail") {
    std::string raw_uuid;
    auto        result = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Whitespace-only string should fail") {
    std::string raw_uuid = "   \t\n  ";
    auto        result   = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Zero significant bytes should fail") {
    std::string raw_uuid = "550e8400-e29b-41d4-a716-446655440000/0";
    auto        result   = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("UUID shorter than significant bytes should fail") {
    std::string raw_uuid = "CAFEBABE/10";  // Only 4 bytes but requests 10
    auto        result   = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Valid UUID with /N notation - 8 bytes") {
    std::string raw_uuid = "550e8400-e29b-41d4-a716-446655440000/8";
    auto        uuid     = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    REQUIRE(uuid.normalized_value == "550e8400e29b41d4a716446655440000");
    REQUIRE(uuid.num_significant_bytes == 8);
  }
  SECTION("Valid UUID with /N notation - 1 byte") {
    std::string raw_uuid = "12-34-56-78-9A-BC-DE-F0-11-22-33-44-55-66-77-88/1";
    auto        uuid     = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    REQUIRE(uuid.normalized_value == "123456789abcdef01122334455667788");
    REQUIRE(uuid.num_significant_bytes == 1);
  }
  SECTION("Valid UUID with /N notation - exactly matches length") {
    std::string raw_uuid = "CAFEBABE/4";
    auto        uuid     = astl::scmi::spec::GetNormalizedUuid(raw_uuid).value();
    REQUIRE(uuid.normalized_value == "cafebabe");
    REQUIRE(uuid.num_significant_bytes == 4);
  }
  SECTION("Malformed / postfix - empty after slash") {
    std::string raw_uuid = "550e8400-e29b-41d4-a716-446655440000/";
    auto        result   = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Malformed / postfix - non-numeric") {
    std::string raw_uuid = "550e8400-e29b-41d4-a716-446655440000/abc";
    auto        result   = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
  SECTION("Malformed / postfix - whitespace") {
    std::string raw_uuid = "550e8400-e29b-41d4-a716-446655440000/  ";
    auto        result   = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(!result.has_value());
    REQUIRE(result.error() == ASTL_STATUS_BAD_ARGUMENT);
  }
}

TEST_CASE("ScmiSpecification::ParseSimple", "[ConfigManager]") {
  std::string raw_scmi_spec = R"json(
{
  "_type": "metrics_specification",
  "document": {
    "timestamp": "2025-11-19",
    "copyright": "2025 Arm Ltd.",
    "confidential": false,
    "quality": "Test",
    "license": "Apache-2.0",
    "description": "Telemetry data capture format layout specification"
  },
  "uuid": "1234",
  "description": "Test Platform Telemetry Data Capture Format Specification",
  "tdcf_instance_id": "[7:0]",
  "chiplet_id": "[15:8]",
  "size": 7016,
  "members": [
    {
      "count": 1,
      "start_offset": 0,
      "block_size": 24,
      "metrics": {
        "UUID": {
          "base_de_id": "0x00000000",
          "name": "UUID",
          "component": "HEADER",
          "description": "SCMI TDCF UUID",
          "unit": "",
          "base10_unit_modifier": 0,
          "rel_offset": "0x0000"
        }
      }
    },
    {
      "count": 6,
      "start_offset": 40,
      "block_size": 48,
      "metrics": {
         "CURRENT_TEMPERATURE": {
          "base_de_id": "0x0000E441",
          "name": "CURRENT_TEMPERATURE",
          "component": "PSS",
          "description": "PSS highest current temperature",
          "unit": "celsius",
          "base10_unit_modifier": -3,
          "rel_offset": "0x0000"
        },
        "MIN_TEMPERATURE": {
          "base_de_id": "0x0000E442",
          "name": "MIN_TEMPERATURE",
          "component": "PSS",
          "description": "PSS lowest temperature since power on",
          "unit": "celsius",
          "base10_unit_modifier": -3,
          "rel_offset": "0x0010"
        },
        "MAX_TEMPERATURE": {
          "base_de_id": "0x0000E443",
          "name": "MAX_TEMPERATURE",
          "component": "PSS",
          "description": "PSS highest temperature since power on",
          "unit": "celsius",
          "base10_unit_modifier": -3,
          "rel_offset": "0x0020"
        }
      }
    }
  ]
}
)json";

  json json_data = json::parse(raw_scmi_spec);

  SECTION("Scmi spec parse file") {
    auto uuid = json_data.at("uuid").get<std::string>();
    REQUIRE(uuid == "1234");
  }

  SECTION("Scmi spec parse members") {
    const auto members = json_data.at("members").get<std::vector<astl::scmi::spec::Member>>();
    REQUIRE(members.size() == 2);
    REQUIRE(members[0].count == 1);
    REQUIRE(members[0].start_offset == 0);
    REQUIRE(members[0].block_size == 24);
  }
}

TEST_CASE("SpecificationDocument parses optional metadata", "[ConfigManager]") {
  SECTION("Present fields are parsed") {
    const json json_data = {
        {"timestamp",    "2026-07-13"                     },
        {"copyright",    "2026 Arm Limited"               },
        {"confidential", true                             },
        {"quality",      "Release"                        },
        {"license",      "Apache-2.0"                     },
        {"description",  "Confidential SCMI specification"},
    };

    const auto document = json_data.get<astl::scmi::spec::SpecificationDocument>();

    REQUIRE(document.timestamp == "2026-07-13");
    REQUIRE(document.copyright == "2026 Arm Limited");
    REQUIRE(document.confidential);
    REQUIRE(document.quality == "Release");
    REQUIRE(document.license == "Apache-2.0");
    REQUIRE(document.description == "Confidential SCMI specification");
  }

  SECTION("Missing fields retain their defaults") {
    const json json_data = {
        {"description", "Minimal SCMI specification"}
    };

    const auto document = json_data.get<astl::scmi::spec::SpecificationDocument>();

    REQUIRE(document.timestamp.empty());
    REQUIRE(document.copyright.empty());
    REQUIRE_FALSE(document.confidential);
    REQUIRE(document.quality.empty());
    REQUIRE(document.license.empty());
    REQUIRE(document.description == "Minimal SCMI specification");
  }
}

TEST_CASE("GetMetricRegistersScmiData", "[ConfigManager]") {
  // define an SCMI specification with 4 instances of a VOLTAGE_RAIL component,
  // each with TEMP_PRESENT and TEMP_MINIMUM_1M registers, and aliases for each instance.
  std::string raw_scmi_spec      = R"json(
  {
    "uuid": "1200/2",
    "description": "Test Platform Telemetry Data Capture Format Specification",
    "tdcf_instance_id": "[7:0]",
    "chiplet_id": "[15:8]",
    "size": 7016,
    "members": [
      {
        "count": 4,
        "start_offset": 0,
        "block_size": 24,
        "metrics": {
          "TEMP_PRESENT": {
            "base_de_id": "0x00004441",
            "name": "TEMP_PRESENT",
            "component": "VOLTAGE_RAIL",
            "description": "Temperature at the present moment",
            "unit": "celsius",
            "base10_unit_modifier": -3,
            "rel_offset": "0x0000"
          },
          "TEMP_MINIMUM_1M": {
            "base_de_id": "0x00004442",
            "name": "TEMP_MINIMUM_1M",
            "component": "VOLTAGE_RAIL",
            "description": "Minimum temperature over 1 minute",
            "unit": "celsius",
            "base10_unit_modifier": -3,
            "rel_offset": "0x0038"
          }
        }
      }
    ],
    "aliases": {
      "VOLTAGE_RAIL.0": "VCPU_C0",
      "VOLTAGE_RAIL.1": "VCPU_C1",
      "VOLTAGE_RAIL.2": "VGPU",
      "VOLTAGE_RAIL.3": "VDDR"
    }
  }
  )json";
  json        json_data          = json::parse(raw_scmi_spec);
  auto        scmi_specification = json_data.get<astl::scmi::spec::ScmiSpecification>();

  // now define metrics to read the TEMP_PRESENT register, which should match all 4 instances, and TEMP_MINIMUM_1M which
  // should also match all 4 instances.
  std::string metrics_definition_json         = R"json(
  {
    "metrics": {
      "CURRENT_TEMPERATURE": {
        "description": "Current Temperature in Celsius, In this example, PSS.5 has a broken temp sensor",
        "metric_type": "value",
        "identifier": "TEMPERATURE",
        "metric_groups": [],
        "collection": {
          "register": "TEMP_PRESENT",
          "protocol": "scmi"
        },
        "disabled": true
      }
    }
  }
  )json";
  json        metrics_json                    = json::parse(metrics_definition_json);
  auto        metrics_declaration             = metrics_json.get<astl::metrics::spec::MetricsDeclaration>();
  auto        current_temp_metric_declaration = metrics_declaration.metrics.at("CURRENT_TEMPERATURE");

  auto scmi_metrics_definitions = GetMetricRegistersScmiData(current_temp_metric_declaration, scmi_specification);
  REQUIRE(scmi_metrics_definitions.size() == 4);
  REQUIRE(scmi_metrics_definitions[0].GetFullyQualifiedName() == "VCPU_C0.0.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[0].de_id == 0x00004441);
  REQUIRE(scmi_metrics_definitions[1].GetFullyQualifiedName() == "VCPU_C1.1.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[1].de_id == 0x00014441);
  REQUIRE(scmi_metrics_definitions[2].GetFullyQualifiedName() == "VGPU.2.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[2].de_id == 0x00024441);
  REQUIRE(scmi_metrics_definitions[3].GetFullyQualifiedName() == "VDDR.3.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[3].de_id == 0x00034441);
}

TEST_CASE("FindSpecFileByUuid prefers the most specific matching UUID entry", "[ConfigManager]") {
  std::string repo_meta_json = R"json(
  {
    "last_updated": "2026-03-16",
    "uuid_mapping": {
      "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000/8": {
        "last_updated": "2026-03-16",
        "description": "generic match",
        "specification_file": "generic.json"
      },
      "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000/16": {
        "last_updated": "2026-03-16",
        "description": "specific match",
        "specification_file": "specific.json"
      }
    }
  }
  )json";

  auto repo_meta = json::parse(repo_meta_json).get<astl::scmi::spec::RepoMeta>();
  auto uuid      = astl::scmi::spec::GetNormalizedUuid("CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000").value();

  auto spec_file = astl::scmi::spec::FindSpecFileByUuid(repo_meta, uuid);

  REQUIRE(spec_file.has_value());
  REQUIRE(spec_file->specification_file == "specific.json");
}

TEST_CASE("FindMetricsFileElementByUuid returns nullopt when there is no matching platform entry", "[ConfigManager]") {
  std::string platform_lookup_json = R"json(
  {
    "last_updated": "2026-03-16",
    "scmi_uuid_mapping": {
      "DEADBEEF-CAFE-BABE-CAFE-BABEBEEF0000": {
        "last_updated": "2026-03-16",
        "description": "other platform",
        "metrics_file": "other.json"
      }
    }
  }
  )json";

  auto platform_lookup = json::parse(platform_lookup_json).get<astl::metrics::spec::PlatformLookup>();
  auto uuid            = astl::scmi::spec::GetNormalizedUuid("CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000").value();

  auto metrics_file = astl::metrics::spec::FindMetricsFileElementByUuid(platform_lookup, uuid);

  REQUIRE_FALSE(metrics_file.has_value());
}

TEST_CASE("PlatformLookup parses optional SCMI target name template", "[ConfigManager]") {
  std::string platform_lookup_json = R"json(
  {
    "last_updated": "2026-03-24",
    "scmi_uuid_mapping": {
      "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000/14": {
        "last_updated": "2026-03-24",
        "description": "test platform",
        "metrics_file": "test.json",
        "name": "{telemetry_subdirectory}"
      }
    }
  }
  )json";

  auto platform_lookup = json::parse(platform_lookup_json).get<astl::metrics::spec::PlatformLookup>();
  auto uuid            = astl::scmi::spec::GetNormalizedUuid("CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000").value();

  auto metrics_file = astl::metrics::spec::FindMetricsFileElementByUuid(platform_lookup, uuid);

  REQUIRE(metrics_file.has_value());
  REQUIRE(metrics_file->name.has_value());
  REQUIRE(*metrics_file->name == "{telemetry_subdirectory}");
}

TEST_CASE("LoadPlatformLookupFragments merges recursive platform lookup files", "[ConfigManager]") {
  const auto    config_root = MakeTempDir("astl_platform_lookup_test_");
  TempFileGuard temp_guard{config_root};

  WriteTextFile(config_root / "metrics" / "vendor_a" / "platform_lookup.json", R"json({
  "last_updated": "2026-03-16",
  "scmi_uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000": {
      "last_updated": "2026-03-16",
      "description": "first platform",
      "metrics_file": "metrics_a.json",
      "name": "first"
    }
  }
}
)json");
  WriteTextFile(config_root / "metrics" / "vendor_b" / "platform_lookup.json", R"json({
  "last_updated": "2026-03-17",
  "scmi_uuid_mapping": {
    "DEADBEEF-CAFE-BABE-CAFE-BABEBEEF0000": {
      "last_updated": "2026-03-17",
      "description": "second platform",
      "metrics_file": "nested/metrics_b.json"
    }
  }
}
)json");
  WriteTextFile(config_root / "metrics" / "vendor_c" / "metrics.json", R"json({"metrics": {}})json");

  auto platform_lookup = astl::config::LoadPlatformLookupFragments(config_root / "metrics");

  REQUIRE(platform_lookup.has_value());
  REQUIRE(platform_lookup->metric_files_by_platform_uuid.size() == 2);
  auto first_uuid = astl::scmi::spec::GetNormalizedUuid("CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000").value();
  auto first      = astl::metrics::spec::FindMetricsFileElementByUuid(*platform_lookup, first_uuid);
  REQUIRE(first.has_value());
  REQUIRE(first->resolved_metrics_file == config_root / "metrics" / "vendor_a" / "metrics_a.json");

  auto second_uuid = astl::scmi::spec::GetNormalizedUuid("DEADBEEF-CAFE-BABE-CAFE-BABEBEEF0000").value();
  auto second      = astl::metrics::spec::FindMetricsFileElementByUuid(*platform_lookup, second_uuid);
  REQUIRE(second.has_value());
  REQUIRE(second->resolved_metrics_file == config_root / "metrics" / "vendor_b" / "nested" / "metrics_b.json");
}

TEST_CASE("LoadRepoMetaFragments merges recursive repometa files", "[ConfigManager]") {
  const auto    config_root = MakeTempDir("astl_repometa_lookup_test_");
  TempFileGuard temp_guard{config_root};

  WriteTextFile(config_root / "scmi" / "public" / "generic" / "repometa.json", R"json({
  "last_updated": "2026-03-16",
  "uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000/8": {
      "last_updated": "2026-03-16",
      "description": "generic",
      "specification_file": "generic.json"
    }
  }
}
)json");
  WriteTextFile(config_root / "scmi" / "public" / "specific" / "repometa.json", R"json({
  "last_updated": "2026-03-17",
  "uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000/16": {
      "last_updated": "2026-03-17",
      "description": "specific",
      "specification_file": "nested/specific.json"
    }
  }
}
)json");
  WriteTextFile(config_root / "scmi" / "public" / "empty" / "scp.json", R"json({"members": []})json");

  auto repo_meta = astl::config::LoadRepoMetaFragments(config_root / "scmi" / "public");

  REQUIRE(repo_meta.has_value());
  REQUIRE(repo_meta->spec_files_by_uuid.size() == 2);
  auto uuid      = astl::scmi::spec::GetNormalizedUuid("CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000").value();
  auto spec_file = astl::scmi::spec::FindSpecFileByUuid(*repo_meta, uuid);
  REQUIRE(spec_file.has_value());
  REQUIRE(spec_file->description == "specific");
  REQUIRE(spec_file->resolved_specification_file ==
          config_root / "scmi" / "public" / "specific" / "nested" / "specific.json");
}

TEST_CASE("MockScmi lookup fragments resolve through mockscmi directories", "[ConfigManager]") {
  const auto    config_root = MakeTempDir("astl_mockscmi_lookup_test_");
  TempFileGuard temp_guard{config_root};

  WriteTextFile(config_root / "scmi" / "public" / "mockscmi" / "repometa.json", R"json({
  "last_updated": "2026-01-13",
  "uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000": {
      "last_updated": "2025-12-18",
      "description": "MockScmi test harness for ASTL development",
      "specification_file": "mockscmi.json",
      "confidential": false
    }
  }
}
)json");
  WriteTextFile(config_root / "metrics" / "mockscmi" / "platform_lookup.json", R"json({
  "last_updated": "2025-12-18",
  "scmi_uuid_mapping": {
    "CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000/14": {
      "last_updated": "2025-12-18",
      "description": "MockScmi test harness for ASTL development",
      "metrics_file": "metrics.json",
      "name": "scmi-mockscmi-{telemetry_subdirectory}",
      "confidential": false
    }
  }
}
)json");

  auto repo_meta       = astl::config::LoadRepoMetaFragments(config_root / "scmi" / "public");
  auto platform_lookup = astl::config::LoadPlatformLookupFragments(config_root / "metrics");

  REQUIRE(repo_meta.has_value());
  REQUIRE(platform_lookup.has_value());

  auto uuid         = astl::scmi::spec::GetNormalizedUuid("CAFEBABE-CAFE-BABE-CAFE-BABEBEEF0000").value();
  auto spec_file    = astl::scmi::spec::FindSpecFileByUuid(*repo_meta, uuid);
  auto metrics_file = astl::metrics::spec::FindMetricsFileElementByUuid(*platform_lookup, uuid);

  REQUIRE(spec_file.has_value());
  REQUIRE(spec_file->resolved_specification_file == config_root / "scmi" / "public" / "mockscmi" / "mockscmi.json");

  REQUIRE(metrics_file.has_value());
  REQUIRE(metrics_file->resolved_metrics_file == config_root / "metrics" / "mockscmi" / "metrics.json");
  REQUIRE(metrics_file->name.has_value());
  REQUIRE(*metrics_file->name == "scmi-mockscmi-{telemetry_subdirectory}");
}

TEST_CASE("GetMetricRegistersScmiData applies unit, component, and instance filters", "[ConfigManager]") {
  std::string raw_scmi_spec = R"json(
  {
    "uuid": "1200/2",
    "description": "Filter test spec",
    "tdcf_instance_id": "[7:0]",
    "chiplet_id": "[15:8]",
    "size": 128,
    "members": [
      {
        "count": 3,
        "start_offset": 0,
        "block_size": 24,
        "metrics": {
          "TEMP_PRESENT": {
            "base_de_id": "0x00004441",
            "name": "TEMP_PRESENT",
            "component": "CPU",
            "description": "Temperature",
            "unit": "celsius",
            "base10_unit_modifier": -3,
            "rel_offset": "0x0000"
          }
        }
      }
    ]
  }
  )json";

  auto scmi_specification = json::parse(raw_scmi_spec).get<astl::scmi::spec::ScmiSpecification>();

  SECTION("component and instance filters narrow the matched metrics") {
    astl::metrics::spec::MetricJsonDeclaration metric_declaration;
    metric_declaration.description         = "CPU temperature";
    metric_declaration.metric_type         = "value";
    metric_declaration.unit                = "C";
    metric_declaration.collection.protocol = "scmi";
    metric_declaration.collection.raw_json = nlohmann::json{
        {"protocol",              "scmi"        },
        {"register",              "TEMP_PRESENT"},
        {"scmi_component_filter", "CPU"         },
        {"scmi_instance_filter",  "1"           }
    };

    auto matches = GetMetricRegistersScmiData(metric_declaration, scmi_specification);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].GetFullyQualifiedName() == "CPU.1.TEMP_PRESENT");
    REQUIRE(matches[0].de_id == 0x00014441);
  }

  SECTION("unit mismatch filters out otherwise matching SCMI registers") {
    astl::metrics::spec::MetricJsonDeclaration metric_declaration;
    metric_declaration.description         = "CPU temperature";
    metric_declaration.metric_type         = "value";
    metric_declaration.unit                = "W";
    metric_declaration.collection.protocol = "scmi";
    metric_declaration.collection.raw_json = nlohmann::json{
        {"protocol", "scmi"        },
        {"register", "TEMP_PRESENT"}
    };

    auto matches = GetMetricRegistersScmiData(metric_declaration, scmi_specification);

    REQUIRE(matches.empty());
  }
}

TEST_CASE("FindMatchingScmiRegistersForResidency rejects non-zero base10 modifiers", "[ConfigManager]") {
  std::string raw_scmi_spec = R"json(
  {
    "uuid": "1200/2",
    "description": "Test Platform Telemetry Data Capture Format Specification",
    "tdcf_instance_id": "[7:0]",
    "chiplet_id": "[15:8]",
    "size": 256,
    "members": [
      {
        "count": 1,
        "start_offset": 0,
        "block_size": 24,
        "metrics": {
          "RESIDENCY_TICKS": {
            "base_de_id": "0x00001000",
            "name": "RESIDENCY_TICKS",
            "component": "CPU",
            "description": "State residency ticks",
            "unit": "none",
            "base10_unit_modifier": -3,
            "rel_offset": "0x0000"
          }
        }
      }
    ]
  }
  )json";

  std::string metrics_definition_json = R"json(
  {
    "metrics": {
      "CPU Residency": {
        "description": "CPU residency metric",
        "unit": "us",
        "metric_type": "residency",
        "identifier": "COUNT",
        "collection": {
          "protocol": "scmi"
        },
        "states": {
          "C1": {
            "register": "RESIDENCY_TICKS",
            "tick_frequency": 1000000,
            "description": "Idle state"
          }
        }
      }
    }
  }
  )json";

  auto scmi_specification  = json::parse(raw_scmi_spec).get<astl::scmi::spec::ScmiSpecification>();
  auto metrics_declaration = json::parse(metrics_definition_json).get<astl::metrics::spec::MetricsDeclaration>();
  auto residency_metric_declaration = metrics_declaration.metrics.at("CPU Residency");

  auto result =
      astl::scmi::spec::FindMatchingScmiRegistersForResidency(residency_metric_declaration, scmi_specification);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == astl::kInternalNotImplemented);
}
