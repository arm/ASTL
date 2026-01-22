#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl_errors.h"
#include "common/scmi/uuid.hpp"
#include "config/scmi_platform_telemetry_spec.hpp"

using json = nlohmann::json;

TEST_CASE("ScmiSpecification::NormalizeUUID", "[ConfigManager]") {
  SECTION("Typical lowercase uuid") {
    std::string raw_uuid  = "550e8400-e29b-41d4-a716-446655440000";
    std::string norm_uuid = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(norm_uuid == "550e8400e29b41d4a716446655440000");
  }
  SECTION("Typical uppercase uuid") {
    std::string raw_uuid  = "CAFEBABE-41D4-A716-446655440000";
    std::string norm_uuid = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(norm_uuid == "cafebabe41d4a716446655440000");
  }
  SECTION("Strip 0x prefix uuid") {
    std::string raw_uuid  = "0xCAFEBABE-41D4-A716-446655440000";
    std::string norm_uuid = astl::scmi::spec::GetNormalizedUuid(raw_uuid);
    REQUIRE(norm_uuid == "cafebabe41d4a716446655440000");
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
  "instance_id": "[7:0]",
  "chiplet_id": "[15:8]",
  "size": 7016,
  "layout": [
    {
      "count": 1,
      "start_offset": 0,
      "block_size": 24,
      "members": [
        {
          "base_de_id": "0x00000000",
          "name": "UUID",
          "component": "HEADER",
          "description": "SCMI TDCF UUID",
          "unit": "",
          "unit_exponent": 0,
          "rel_offset": "0x0000"
        }
      ]
    },
    {
      "count": 6,
      "start_offset": 40,
      "block_size": 48,
      "members": [
        {
          "base_de_id": "0x0000E441",
          "name": "CURRENT_TEMPERATURE",
          "component": "PSS",
          "description": "PSS highest current temperature",
          "unit": "Celcius",
          "unit_exponent": -3,
          "rel_offset": "0x0000"
        },
        {
          "base_de_id": "0x0000E442",
          "name": "MIN_TEMPERATURE",
          "component": "PSS",
          "description": "PSS lowest temperature since power on",
          "unit": "Celcius",
          "unit_exponent": -3,
          "rel_offset": "0x0010"
        },
        {
          "base_de_id": "0x0000E443",
          "name": "MAX_TEMPERATURE",
          "component": "PSS",
          "description": "PSS highest temperature since power on",
          "unit": "Celcius",
          "unit_exponent": -3,
          "rel_offset": "0x0020"
        }
      ]
    }
  ]
}
)json";

  json json_data = json::parse(raw_scmi_spec);

  SECTION("Scmi spec parse file") {
    auto uuid = json_data.at("uuid").get<std::string>();
    REQUIRE(uuid == "1234");
  }

  SECTION("Scmi spec parse layout") {
    std::vector<astl::scmi::spec::Layout> layouts = json_data.at("layout").get<std::vector<astl::scmi::spec::Layout>>();
    REQUIRE(layouts.size() == 2);
    REQUIRE(layouts[0].count == 1);
    REQUIRE(layouts[0].start_offset == 0);
    REQUIRE(layouts[0].block_size == 24);
  }
}
