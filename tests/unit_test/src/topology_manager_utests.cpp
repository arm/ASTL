// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
#include "config/astl_configuration.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "serdes/targets.pb.h"
#include "target.hpp"
#include "topology/scmi_target.hpp"
#include "topology/scmi_topology_plugin.hpp"
#include "topology/topology_builder.hpp"
#include "topology/topology_manager.hpp"

using trompeloeil::_;
using trompeloeil::re;

namespace fs = std::filesystem;

namespace {

void WriteSerializedTopologyCache(const fs::path& cache_dir) {
  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  REQUIRE(!ec);

  std::vector<std::unique_ptr<astl::ITarget>> targets;
  targets.push_back(
      std::make_unique<astl::ScmiTarget>("package0 telemetry", "unit-test target", "tlm-0", nullptr, "1234"));
  astl::TopologyManager topology_manager{std::move(targets)};

  std::ofstream topology_file(cache_dir / astl::kTopologyManagerFileName, std::ios::binary | std::ios::out);
  REQUIRE(topology_file.good());
  REQUIRE(astl::ProtobufSerDes::Serialize(topology_manager, topology_file) == ASTL_STATUS_SUCCESS);
}

}  // namespace

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
  REQUIRE((*targets)[0]->Name() == "scmi_tlm-0");
  REQUIRE((*targets)[1]->Name() == "scmi_tlm-1");
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[0].get()) != nullptr);
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[1].get()) != nullptr);
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[0].get())->TelemetrySubdirectory() == "tlm-0");
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[1].get())->TelemetrySubdirectory() == "tlm-1");
}

TEST_CASE("TopologyBuilder::BuildTopologyManager rejects load_file_path without cache dir", "[TopologyManager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  auto result = astl::BuildTopologyManager(configuration, std::nullopt);

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_BAD_CONFIGURATION);
}

TEST_CASE("TopologyBuilder::BuildTopologyManagerFromASTLFile fails when topology file is missing",
          "[TopologyManager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  const fs::path cache_dir = fs::temp_directory_path() / "astl_topology_builder_missing_topology";
  TempFileGuard  cache_guard(cache_dir);

  std::error_code ec;
  fs::remove_all(cache_dir, ec);
  fs::create_directories(cache_dir, ec);
  REQUIRE(!ec);

  auto result = astl::BuildTopologyManager(configuration, cache_dir);

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_INTERNAL_ERROR);
}

TEST_CASE("TopologyBuilder::BuildTopologyManagerFromASTLFile fails on corrupt topology protobuf", "[TopologyManager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  const fs::path cache_dir = fs::temp_directory_path() / "astl_topology_builder_corrupt_topology";
  TempFileGuard  cache_guard(cache_dir);

  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  REQUIRE(!ec);
  {
    std::ofstream topology_file(cache_dir / astl::kTopologyManagerFileName, std::ios::out | std::ios::trunc);
    REQUIRE(topology_file.good());
    astl::protobuf::TargetList target_list;
    target_list.add_targets();  // empty name should be rejected during topology rebuild
    REQUIRE(target_list.SerializeToOstream(&topology_file));
  }

  auto result = astl::BuildTopologyManager(configuration, cache_dir);

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error() == ASTL_STATUS_INVALID_VALUE_TYPE);
}

TEST_CASE("TopologyBuilder::BuildTopologyManagerFromASTLFile rebuilds a serialized topology manager",
          "[TopologyManager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration           = configuration_result.value();
  configuration.load_file_path = "/tmp/unit_test_session.astl";

  const fs::path cache_dir = fs::temp_directory_path() / "astl_topology_builder_valid_topology";
  TempFileGuard  cache_guard(cache_dir);
  WriteSerializedTopologyCache(cache_dir);

  auto result = astl::BuildTopologyManager(configuration, cache_dir);

  REQUIRE(result.has_value());
  REQUIRE(result.value() != nullptr);
  REQUIRE(result.value()->GetTargets().size() == 1);
  REQUIRE(result.value()->GetTargets()[0]->Name() == "package0 telemetry");
  const auto* scmi_target = dynamic_cast<const astl::ScmiTarget*>(result.value()->GetTargets()[0].get());
  REQUIRE(scmi_target != nullptr);
  REQUIRE(scmi_target->TelemetrySubdirectory() == "tlm-0");
}
