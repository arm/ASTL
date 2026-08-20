// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <string_view>

#include "../../mock_classes.hpp"
#include "../../test_includes.hpp"  // include before catch2
#include "../../test_utilities.hpp"
#include "config/astl_configuration.hpp"
#include "serdes/protobuf_serdes.hpp"
#include "serdes/targets.pb.h"
#include "target.hpp"
#if defined(ASTL_INCLUDE_PROCFS)
#  include "topology/procfs_topology_plugin.hpp"
#endif
#include "topology/scmi_target.hpp"
#include "topology/scmi_topology_plugin.hpp"
#include "topology/topology_builder.hpp"
#include "topology/topology_manager.hpp"

using trompeloeil::_;
using trompeloeil::re;

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

struct ScriptedScmiFileInterface {
  fs::path                                                          base_path;
  std::expected<bool, astl_status_code>                             is_valid_result{true};
  std::expected<bool, astl_status_code>                             has_read_permission_result{true};
  astl_status_code                                                  read_status{ASTL_STATUS_SUCCESS};
  std::string                                                       read_content{"CAFEBABECAFEBABECAFEBABEBEEF0000"};
  std::expected<std::vector<fs::directory_entry>, astl_status_code> subdirectories_result{
      std::vector<fs::directory_entry>{}};

  auto GetBasePath() const -> const fs::path& { return base_path; }
  auto IsValid(const fs::path& /*path*/) const noexcept -> std::expected<bool, astl_status_code> {
    return is_valid_result;
  }
  auto HasReadPermission(const fs::path& /*path*/) const noexcept -> std::expected<bool, astl_status_code> {
    return has_read_permission_result;
  }
  auto Read(const fs::path& /*path*/, std::string& output) const -> astl_status_code {
    output = read_content;
    return read_status;
  }
  auto GetSubdirectories() const -> std::expected<std::vector<fs::directory_entry>, astl_status_code> {
    return subdirectories_result;
  }
};

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
  REQUIRE((*targets)[0]->CollectorTargetPath().has_value());
  REQUIRE(*(*targets)[0]->CollectorTargetPath() == "tlm-0");
  REQUIRE((*targets)[1]->Name() == "scmi_tlm-1");
  REQUIRE((*targets)[1]->CollectorTargetPath().has_value());
  REQUIRE(*(*targets)[1]->CollectorTargetPath() == "tlm-1");
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[0].get()) != nullptr);
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[1].get()) != nullptr);
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[0].get())->TelemetrySubdirectory() == "tlm-0");
  REQUIRE(dynamic_cast<astl::ScmiTarget*>((*targets)[1].get())->TelemetrySubdirectory() == "tlm-1");
}

TEST_CASE("Topology::ScmiPlugin handles sysfs target metadata failures", "[TopologyManager]") {
  const fs::path scmi_sysfs_path{"/tmp/fake/scmi/sysfs"};

  SECTION("validity check error is returned") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.is_valid_result = std::unexpected(ASTL_STATUS_FILE_ERROR);

    auto target = astl::ScmiTopologyPlugin::detail::DetectTarget(file_interface, "tlm-0");

    REQUIRE_FALSE(target.has_value());
    REQUIRE(target.error() == ASTL_STATUS_FILE_ERROR);
  }

  SECTION("missing implementation version file is skipped") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.is_valid_result = false;

    auto target = astl::ScmiTopologyPlugin::detail::DetectTarget(file_interface, "tlm-0");

    REQUIRE(target.has_value());
    REQUIRE(target.value() == nullptr);
  }

  SECTION("permission check error is returned") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.has_read_permission_result = std::unexpected(ASTL_STATUS_FILE_ERROR);

    auto target = astl::ScmiTopologyPlugin::detail::DetectTarget(file_interface, "tlm-0");

    REQUIRE_FALSE(target.has_value());
    REQUIRE(target.error() == ASTL_STATUS_FILE_ERROR);
  }

  SECTION("unreadable implementation version file is skipped") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.has_read_permission_result = false;

    auto target = astl::ScmiTopologyPlugin::detail::DetectTarget(file_interface, "tlm-0");

    REQUIRE(target.has_value());
    REQUIRE(target.value() == nullptr);
  }

  SECTION("read error is returned") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.read_status = ASTL_STATUS_FILE_ERROR;

    auto target = astl::ScmiTopologyPlugin::detail::DetectTarget(file_interface, "tlm-0");

    REQUIRE_FALSE(target.has_value());
    REQUIRE(target.error() == ASTL_STATUS_FILE_ERROR);
  }

  SECTION("invalid UUID content is returned as an error") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.read_content = "   ";

    auto target = astl::ScmiTopologyPlugin::detail::DetectTarget(file_interface, "tlm-0");

    REQUIRE_FALSE(target.has_value());
  }
}

TEST_CASE("Topology::ScmiPlugin handles sysfs root listing outcomes", "[TopologyManager]") {
  const fs::path scmi_sysfs_path{"/tmp/fake/scmi/sysfs"};

  SECTION("root validity check error is returned") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.is_valid_result = std::unexpected(ASTL_STATUS_FILE_ERROR);

    auto directories = astl::ScmiTopologyPlugin::detail::ListSortedTelemetryDirectories(file_interface);

    REQUIRE_FALSE(directories.has_value());
    REQUIRE(directories.error() == ASTL_STATUS_FILE_ERROR);
  }

  SECTION("missing root returns an empty list") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.is_valid_result = false;

    auto directories = astl::ScmiTopologyPlugin::detail::ListSortedTelemetryDirectories(file_interface);

    REQUIRE(directories.has_value());
    REQUIRE(directories->empty());
  }

  SECTION("child listing failure returns an empty list") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.subdirectories_result = std::unexpected(ASTL_STATUS_FILE_ERROR);

    auto directories = astl::ScmiTopologyPlugin::detail::ListSortedTelemetryDirectories(file_interface);

    REQUIRE(directories.has_value());
    REQUIRE(directories->empty());
  }

  SECTION("telemetry directories are sorted in natural order") {
    ScriptedScmiFileInterface file_interface{.base_path = scmi_sysfs_path};
    file_interface.subdirectories_result = std::vector<fs::directory_entry>{
        fs::directory_entry{scmi_sysfs_path / "tlm-10"},
        fs::directory_entry{scmi_sysfs_path / "tlm-1"},
        fs::directory_entry{scmi_sysfs_path / "tlm-2"},
    };

    auto directories = astl::ScmiTopologyPlugin::detail::ListSortedTelemetryDirectories(file_interface);

    REQUIRE(directories.has_value());
    REQUIRE(directories->size() == 3);
    REQUIRE((*directories)[0].path().filename() == "tlm-1");
    REQUIRE((*directories)[1].path().filename() == "tlm-2");
    REQUIRE((*directories)[2].path().filename() == "tlm-10");
  }
}

TEST_CASE("Topology::ScmiPlugin lists ioctl devices in deterministic order", "[TopologyManager]") {
  const fs::path ioctl_root = fs::temp_directory_path() / "astl_topology_ioctl_device_list";
  TempFileGuard  ioctl_guard(ioctl_root);

  WriteTextFile(ioctl_root / "tlm_10", "regular file, not a device");
  WriteTextFile(ioctl_root / "tlm_1", "regular file, not a device");
  WriteTextFile(ioctl_root / "tlm_2", "regular file, not a device");
  WriteTextFile(ioctl_root / "tlm_a", "not a telemetry device");
  WriteTextFile(ioctl_root / "other", "not a telemetry device");

  auto devices = astl::ScmiTopologyPlugin::detail::ListSortedIoctlDevices(ioctl_root);

  REQUIRE(devices.has_value());
  REQUIRE(devices->size() == 3);
  REQUIRE((*devices)[0].path().filename() == "tlm_1");
  REQUIRE((*devices)[1].path().filename() == "tlm_2");
  REQUIRE((*devices)[2].path().filename() == "tlm_10");

  auto targets = astl::ScmiTopologyPlugin::detail::BuildTargetsFromIoctlDevices(*devices);
  REQUIRE(targets.has_value());
  REQUIRE(targets->empty());
}

TEST_CASE("Topology::ScmiPlugin scans real sysfs fixtures through backend selection", "[TopologyManager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration = configuration_result.value();

  const fs::path sysfs_root = fs::temp_directory_path() / "astl_topology_scmi_scan_sysfs";
  const fs::path ioctl_root = fs::temp_directory_path() / "astl_topology_scmi_scan_ioctl";
  TempFileGuard  sysfs_guard(sysfs_root);
  TempFileGuard  ioctl_guard(ioctl_root);

  configuration.scmi_sysfs_telemetry_root_path = sysfs_root;
  configuration.scmi_ioctl_device_root_path    = ioctl_root;
  WriteTextFile(sysfs_root / "tlm-0" / "de_implementation_version", "CAFEBABECAFEBABECAFEBABEBEEF0000");

  SECTION("auto mode falls back to sysfs when no ioctl target is usable") {
    EnvVarGuard backend_guard(astl::EnvVar::ASTL_SCMI_INTERFACE, "auto");

    auto targets = astl::ScmiTopologyPlugin::ScanForTargets(configuration);

    REQUIRE(targets.has_value());
    REQUIRE(targets->size() == 1);
    REQUIRE((*targets)[0]->Name() == "scmi_tlm-0");
  }

  SECTION("forced sysfs mode skips ioctl discovery") {
    EnvVarGuard backend_guard(astl::EnvVar::ASTL_SCMI_INTERFACE, "sysfs");

    auto targets = astl::ScmiTopologyPlugin::ScanForTargets(configuration);

    REQUIRE(targets.has_value());
    REQUIRE(targets->size() == 1);
    REQUIRE((*targets)[0]->Name() == "scmi_tlm-0");
  }

  SECTION("forced ioctl mode does not fall back to sysfs") {
    EnvVarGuard backend_guard(astl::EnvVar::ASTL_SCMI_INTERFACE, "ioctl");

    auto targets = astl::ScmiTopologyPlugin::ScanForTargets(configuration);

    REQUIRE(targets.has_value());
    REQUIRE(targets->empty());
  }
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

TEST_CASE("TopologyBuilder::BuildTopologyManager honors disabled collectors", "[TopologyManager]") {
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());
  auto configuration       = configuration_result.value();
  configuration.collectors = astl::CollectorSelection{.scmi = false, .libsensors = false, .procfs = false};

  auto result = astl::BuildTopologyManager(configuration, std::nullopt);

  REQUIRE(result.has_value());
  REQUIRE(result.value()->GetTargets().empty());
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
  REQUIRE(result.value()->GetTargets()[0]->CollectorTargetPath().has_value());
  REQUIRE(*result.value()->GetTargets()[0]->CollectorTargetPath() == "tlm-0");
  REQUIRE(scmi_target->TelemetrySubdirectory() == "tlm-0");
}

TEST_CASE("Topology::ProcfsPlugin", "[TopologyManager]") {
#if !defined(ASTL_INCLUDE_PROCFS)
  SKIP("ASTL was built without procfs support");
#else
  auto configuration_result = astl::AstlConfiguration::CreateConfiguration();
  REQUIRE(configuration_result.has_value());

  const fs::path  procfs_root = fs::temp_directory_path() / "astl_topology_procfs_fixture";
  TempFileGuard   procfs_guard(procfs_root);
  std::error_code ec;
  fs::create_directories(procfs_root, ec);
  REQUIRE(!ec);

  {
    std::ofstream stat_file(procfs_root / "stat", std::ios::out | std::ios::trunc);
    REQUIRE(stat_file.good());
    stat_file << "cpu 1 2 3 4\n";
  }

  astl::FileInterface procfs_file_interface{procfs_root};
  auto targets = astl::ProcfsTopologyPlugin::detail::ScanForTargetsOnFileInterface(configuration_result.value(),
                                                                                   std::move(procfs_file_interface));
  REQUIRE(targets.has_value());
  REQUIRE(targets->size() == 1);
  REQUIRE((*targets)[0]->Name() == "procfs");
  REQUIRE((*targets)[0]->GetCollectorType() == astl::CollectorType::PROCFS);
#endif
}
