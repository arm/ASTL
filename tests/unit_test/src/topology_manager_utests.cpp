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

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "config/astl_configuration.hpp"
#include "target.hpp"
#include "topology/scmi_topology_plugin.hpp"

using trompeloeil::_;
using trompeloeil::re;

TEST_CASE("Topology::ScmiPlugin", "[TopologyManager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  MockFileInterface     mock_file_interface;
  std::filesystem::path scmi_sysfs_path{"/tmp/fake/scmi/sysfs"};
  ALLOW_CALL(mock_file_interface, GetBasePath()).RETURN(scmi_sysfs_path);
  // scmi_topology_plugin looks at `de_implementation_version` file, so let it 'read' it
  constexpr auto failed_uuid_file = "/tmp/fake/scmi/sysfs/not_a_target/de_implementation_version";
  constexpr auto tlm0_uuid_file   = "/tmp/fake/scmi/sysfs/tlm-0/de_implementation_version";
  constexpr auto tlm1_uuid_file   = "/tmp/fake/scmi/sysfs/tlm-1/de_implementation_version";

  ALLOW_CALL(mock_file_interface, IsValid(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, IsValid(failed_uuid_file)).RETURN(false);
  ALLOW_CALL(mock_file_interface, HasReadPermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, Read(_, _)).RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(mock_file_interface, Read(tlm0_uuid_file, _)).SIDE_EFFECT(_2 = "0000").RETURN(ASTL_STATUS_SUCCESS);
  REQUIRE_CALL(mock_file_interface, Read(tlm1_uuid_file, _)).SIDE_EFFECT(_2 = "0001").RETURN(ASTL_STATUS_SUCCESS);
  // match anything other than a tlm UUID
  ALLOW_CALL(mock_file_interface, GetSubdirectories())
      .RETURN(std::vector<std::filesystem::directory_entry>{
          std::filesystem::directory_entry{scmi_sysfs_path / "tlm-0"},
          std::filesystem::directory_entry{scmi_sysfs_path / "not_a_target"},
          std::filesystem::directory_entry{scmi_sysfs_path / "tlm-1"},
      });

  auto targets =
      astl::ScmiTopologyPlugin::detail::ScanForTargetsOnFileInterface(configuration, std::move(mock_file_interface));
  REQUIRE(targets.has_value());
  REQUIRE(targets->size() == 2);
  REQUIRE((*targets)[0]->Name() == "tlm-0");
  REQUIRE((*targets)[1]->Name() == "tlm-1");
}