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

TEST_CASE("Topology::ScmiPlugin", "[TopologyManager]") {
  auto configuration = astl::ParseConfiguration("{\"metrics\": {}}");
  REQUIRE(configuration.has_value());

  MockFileInterface     mock_file_interface;
  std::filesystem::path scmi_sysfs_path{"/tmp/fake/scmi/sysfs"};
  ALLOW_CALL(mock_file_interface, GetBasePath()).RETURN(scmi_sysfs_path);
  // scmi_topology_plugin looks at `de_implementation_version` file, so let it 'read' it
  ALLOW_CALL(mock_file_interface, IsValid(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, HasReadPermission(_)).RETURN(true);
  ALLOW_CALL(mock_file_interface, Read(_, _)).RETURN(ASTL_STATUS_SUCCESS);

  auto targets = astl::ScmiTopologyPlugin::detail::ScanForTargetsOnFileInterface(configuration.value(),
                                                                                 std::move(mock_file_interface));
  REQUIRE(targets.has_value());
  /// @todo ASTL-165 Get rid of hard-coded AP0 name
  REQUIRE(targets->size() == 1);
}