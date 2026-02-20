#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl_errors.h"
#include "common/scmi/uuid.hpp"
#include "config/metric_json_declaration.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"

using json = nlohmann::json;

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
    std::string raw_uuid = "";
    auto        result   = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
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
    "copyright": "Copyright 2025 Arm Ltd.",
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
        "category": "TEMPERATURE",
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
  REQUIRE(scmi_metrics_definitions[0].GetFullyQualifiedName() == "VCPU_C0.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[0].de_id == 0x00004441);
  REQUIRE(scmi_metrics_definitions[1].GetFullyQualifiedName() == "VCPU_C1.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[1].de_id == 0x00014441);
  REQUIRE(scmi_metrics_definitions[2].GetFullyQualifiedName() == "VGPU.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[2].de_id == 0x00024441);
  REQUIRE(scmi_metrics_definitions[3].GetFullyQualifiedName() == "VDDR.TEMP_PRESENT");
  REQUIRE(scmi_metrics_definitions[3].de_id == 0x00034441);
}
