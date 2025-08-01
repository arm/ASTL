#include "../../test_includes.hpp"  // include before catch2
#include "astl/astl_errors.h"
#include "config/scmi_specification_json.hpp"

using json = nlohmann::json;

TEST_CASE("ScmiSpecification::ParseSimple", "[ConfigManager]") {
  std::string raw_scmi_spec = R"json(
{
   "definitions": {
      "_description": "Collections of individual data sources and group data sources. These will be referred by data source specification",
      "smcf_mgi": {}
   },
   "transformations": {
      "_description": "Collections of methods to transform raw data sources to produce a value or to classify/bin a value",
      "copy_word": {
         "src": {},
         "dest": {}
      },
      "copy_dword": {
         "src": {},
         "dest": {}
      },
      "copy_long": {
         "src": {},
         "dest": {}
      },
      "maxdwordn": {
         "count": 0,
         "src": {},
         "dest": {}
      },
      "maxlongn": {
         "count": 0,
         "src": {},
         "dest": {}
      }
   },
   "datasources": {
      "lcp": {
         "base_uuid": "0xCAFEBABECAFEBABECAFEBABEBEEF0000",
         "base_address": "0xB0400000",
         "config_size": "0x00200000",
         "mgi_base_address": "0x00040000",
         "mgi_config_size": "0x00001000",
         "smcf_container_base_addr": "0x00100000",
         "smcf_container_size": "0x100",
         "sources": {
            "AP0": {
               "name": "AP0",
               "description": "AP 0 Telemetry source",
               "index": 0,
               "smcf_sources": {
                  "AMU": {
                     "source_type": "smcf_mgi_dma",
                     "description": "CPU AMU SMCF data source definition",
                     "config_registers": {
                     },
                     "data_storage": {
                        "CPU_CYCLES": {
                           "description": "Core Frequency Cycle",
                           "offset": "0x00",
                           "size": 4,
                           "name": "SMCF_SRAM_AP0_AMU_CPU_CYCLES",
                           "addr": "0x00100000"
                        }
                     },
                     "mgi_index": 1,
                     "settings": {
                        "WRADDR0": "0x00100000"
                     },
                     "smcf_container_index": 0
                  }
               }
            }
         }
      }
   },
   "processes": {
      "AP0": {
         "CPU_CYCLES": {
            "copy_long": {
               "src": {
                  "description": "Core Frequency Cycle",
                  "offset": "0x00",
                  "size": 4,
                  "name": "SMCF_SRAM_AP0_AMU_CPU_CYCLES",
                  "addr": "0x00100000"
               },
               "dest": {
                  "instance_id": 0,
                  "local_id": 1,
                  "offset": "0x0010",
                  "name": "AP0_CPU_CYCLES",
                  "line_addr": "0x74008010",
                  "value_addr": "0x74008018",
                  "de_id": "0x00000001"
               }
            }
         }
      }
   },
   "layout": {
      "base_addr": "0x74008000",
      "members": {
         "AP0": {
            "CPU_CYCLES": {
               "instance_id": 0,
               "local_id": 1,
               "offset": "0x0010",
               "name": "AP0_CPU_CYCLES",
               "line_addr": "0x74008010",
               "value_addr": "0x74008018",
               "de_id": "0x00000001"
            }
         }
      }
   }
}
)json";

  json json_data = json::parse(raw_scmi_spec);

  SECTION("Scmi spec parse definitions") {
    auto definitions = json_data.at("definitions").get<astl::scmi::Definitions>();
    REQUIRE(definitions.description.contains("Collections of individual data sources and group data sources. "));
    REQUIRE(definitions.description.contains("These will be referred by data source specification"));
  }

  SECTION("Scmi spec parse layout") {
    const auto& layout = json_data.at("layout").get<astl::scmi::Layout>();
    REQUIRE(layout.base_addr == 0x74008000);
    REQUIRE(layout.members.size() == 1);
    const auto& metrics    = layout.members.at("AP0");
    const auto& cpu_cycles = metrics.at("CPU_CYCLES");
    REQUIRE(cpu_cycles.de_id == 0x00000001);
  }
}
