// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "../../test_includes.hpp"
#include "../../test_utilities.hpp"
#include "collector/scmi_backend_selection.hpp"
#include "collector/scmi_ioctl_interface.hpp"

namespace fs = std::filesystem;

namespace {

void WriteTextFile(const fs::path& path, std::string_view contents) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  REQUIRE_FALSE(ec);

  std::ofstream out(path, std::ios::out | std::ios::trunc);
  REQUIRE(out.good());
  out << contents;
  REQUIRE(out.good());
}

}  // namespace

TEST_CASE("GetScmiBackendPreference reads ASTL_SCMI_INTERFACE", "[scmi_ioctl_interface]") {
  EnvVarGuard guard(astl::EnvVar::ASTL_SCMI_INTERFACE, "");

  SECTION("unset or empty selects automatic backend selection") {
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::AUTO);
  }

  SECTION("auto selects automatic backend selection") {
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "auto") == ASTL_STATUS_SUCCESS);
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::AUTO);
  }

  SECTION("ioctl forces the ioctl backend") {
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "ioctl") == ASTL_STATUS_SUCCESS);
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::IOCTL);
  }

  SECTION("legacy iocl spelling forces the ioctl backend") {
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "iocl") == ASTL_STATUS_SUCCESS);
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::IOCTL);
  }

  SECTION("sysfs forces the sysfs backend") {
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "sysfs") == ASTL_STATUS_SUCCESS);
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::SYSFS);
  }

  SECTION("values are case insensitive") {
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "IoCtL") == ASTL_STATUS_SUCCESS);
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::IOCTL);

    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "SySfS") == ASTL_STATUS_SUCCESS);
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::SYSFS);
  }

  SECTION("unknown values fall back to automatic backend selection") {
    REQUIRE(astl::SetEnvVar(astl::EnvVar::ASTL_SCMI_INTERFACE, "unsupported") == ASTL_STATUS_SUCCESS);
    REQUIRE(astl::GetScmiBackendPreference() == astl::ScmiBackendPreference::AUTO);
  }
}

TEST_CASE("SCMI backend preference helpers describe allowed backends", "[scmi_ioctl_interface]") {
  REQUIRE(astl::ScmiPreferenceAllowsIoctl(astl::ScmiBackendPreference::AUTO));
  REQUIRE(astl::ScmiPreferenceAllowsSysfs(astl::ScmiBackendPreference::AUTO));

  REQUIRE(astl::ScmiPreferenceAllowsIoctl(astl::ScmiBackendPreference::IOCTL));
  REQUIRE_FALSE(astl::ScmiPreferenceAllowsSysfs(astl::ScmiBackendPreference::IOCTL));

  REQUIRE_FALSE(astl::ScmiPreferenceAllowsIoctl(astl::ScmiBackendPreference::SYSFS));
  REQUIRE(astl::ScmiPreferenceAllowsSysfs(astl::ScmiBackendPreference::SYSFS));
}

TEST_CASE("ScmiIoctlInterface converts between target paths and device names", "[scmi_ioctl_interface]") {
  const fs::path device_root = fs::path{"dev"} / "scmi";

  SECTION("legacy telemetry target spelling maps to ioctl device spelling") {
    REQUIRE(astl::ScmiIoctlInterface::DevicePathFromTelemetrySubdirectory(device_root, "tlm-12") ==
            device_root / "tlm_12");
  }

  SECTION("non-telemetry target paths are preserved") {
    REQUIRE(astl::ScmiIoctlInterface::DevicePathFromTelemetrySubdirectory(device_root, "cluster-0") ==
            device_root / "cluster-0");
  }

  SECTION("ioctl device spelling maps back to legacy telemetry target spelling") {
    REQUIRE(astl::ScmiIoctlInterface::TelemetrySubdirectoryFromDeviceName("tlm_7") == "tlm-7");
  }

  SECTION("non-telemetry device names are preserved") {
    REQUIRE(astl::ScmiIoctlInterface::TelemetrySubdirectoryFromDeviceName("sensor_7") == "sensor_7");
  }
}

TEST_CASE("ScmiIoctlInterface identifies likely telemetry ioctl device names", "[scmi_ioctl_interface]") {
  REQUIRE(astl::ScmiIoctlInterface::IsLikelyTelemetryDeviceName("tlm_0"));
  REQUIRE(astl::ScmiIoctlInterface::IsLikelyTelemetryDeviceName("tlm_42"));

  REQUIRE_FALSE(astl::ScmiIoctlInterface::IsLikelyTelemetryDeviceName(""));
  REQUIRE_FALSE(astl::ScmiIoctlInterface::IsLikelyTelemetryDeviceName("tlm_"));
  REQUIRE_FALSE(astl::ScmiIoctlInterface::IsLikelyTelemetryDeviceName("tlm_a"));
  REQUIRE_FALSE(astl::ScmiIoctlInterface::IsLikelyTelemetryDeviceName("tlm-0"));
  REQUIRE_FALSE(astl::ScmiIoctlInterface::IsLikelyTelemetryDeviceName("tlm_1_extra"));
}

TEST_CASE("SCMI ioctl UAPI mirrors the V7 layouts and request sizes", "[scmi_ioctl_interface]") {
  CHECK(sizeof(scmi_tlm_config) == 24);
  CHECK(sizeof(scmi_tlm_de_config) == 48);
  CHECK(sizeof(scmi_tlm_de_info) == 72);
  CHECK(sizeof(scmi_tlm_batch) == 32);
#if defined(__linux__)
  CHECK(_IOC_SIZE(SCMI_TLM_GET_CFG) == sizeof(scmi_tlm_config));
  CHECK(_IOC_SIZE(SCMI_TLM_GET_DE_CFG) == sizeof(scmi_tlm_batch));
  CHECK(_IOC_SIZE(SCMI_TLM_SET_DE_CFG) == sizeof(scmi_tlm_batch));
  // V7 itself encodes data_read for BATCH_READ even though its handler consumes a batch.
  CHECK(_IOC_SIZE(SCMI_TLM_BATCH_READ) == sizeof(scmi_tlm_data_read));
#endif
}

TEST_CASE("ScmiIoctlInterface reports a missing ioctl device without hardware", "[scmi_ioctl_interface]") {
  const fs::path  base_path      = fs::temp_directory_path() / "astl_missing_scmi_ioctl_interface_test";
  const fs::path  missing_device = base_path / "tlm_0";
  TempFileGuard   cleanup(base_path);
  std::error_code error_code;
  fs::remove_all(base_path, error_code);
  REQUIRE_FALSE(error_code);

  astl::ScmiIoctlInterface ioctl_interface{missing_device};
  const auto               open_status = ioctl_interface.Open();

#if defined(__linux__)
  REQUIRE(open_status == ASTL_STATUS_NO_TARGET_FOUND);
  REQUIRE_FALSE(ioctl_interface.IsOpen());

  auto target_available = astl::ScmiIoctlTargetAvailable(missing_device);
  REQUIRE(target_available.has_value());
  REQUIRE_FALSE(*target_available);

  auto data_event_exists = astl::ScmiIoctlDataEventExists(missing_device, 0x1234U);
  REQUIRE(data_event_exists.has_value());
  REQUIRE_FALSE(*data_event_exists);
#else
  REQUIRE(open_status == ASTL_STATUS_NOT_SUPPORTED);

  auto target_available = astl::ScmiIoctlTargetAvailable(missing_device);
  REQUIRE_FALSE(target_available.has_value());
  REQUIRE(target_available.error() == ASTL_STATUS_NOT_SUPPORTED);

  auto data_event_exists = astl::ScmiIoctlDataEventExists(missing_device, 0x1234U);
  REQUIRE_FALSE(data_event_exists.has_value());
  REQUIRE(data_event_exists.error() == ASTL_STATUS_NOT_SUPPORTED);
#endif
}

TEST_CASE("ScmiIoctlInterface opens regular files but reports unsupported ioctl requests", "[scmi_ioctl_interface]") {
  const fs::path base_path   = fs::temp_directory_path() / "astl_regular_scmi_ioctl_interface_test";
  const fs::path device_path = base_path / "tlm_0";
  TempFileGuard  cleanup(base_path);
  WriteTextFile(device_path, "not an ioctl device");

  astl::ScmiIoctlInterface ioctl_interface{device_path};
  REQUIRE(ioctl_interface.DevicePath() == device_path);

#if defined(__linux__)
  REQUIRE(ioctl_interface.Open() == ASTL_STATUS_SUCCESS);
  REQUIRE(ioctl_interface.IsOpen());
  REQUIRE(ioctl_interface.Open() == ASTL_STATUS_SUCCESS);

  astl::ScmiIoctlInterface moved_interface{std::move(ioctl_interface)};
  REQUIRE(moved_interface.IsOpen());
  REQUIRE_FALSE(ioctl_interface.IsOpen());

  astl::ScmiIoctlInterface assigned_interface;
  assigned_interface = std::move(moved_interface);
  REQUIRE(assigned_interface.IsOpen());
  REQUIRE_FALSE(moved_interface.IsOpen());

  REQUIRE(assigned_interface.Probe() == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE_FALSE(assigned_interface.DeImplementationVersion().has_value());
  REQUIRE_FALSE(assigned_interface.DataEventCount().has_value());
  REQUIRE_FALSE(assigned_interface.SupportsSingleRead().has_value());

  scmi_tlm_config config{};
  REQUIRE(assigned_interface.GetConfig(config) == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE(assigned_interface.SetConfig(config) == ASTL_STATUS_NOT_SUPPORTED);

  scmi_tlm_de_config data_event_config{};
  REQUIRE(assigned_interface.GetDataEventConfig(0x1234U, data_event_config) == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE(data_event_config.id == 0x1234U);

  data_event_config.enable   = 17U;
  data_event_config.t_enable = 2U;
  REQUIRE(assigned_interface.SetDataEventConfig(data_event_config) == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE(data_event_config.enable == 1U);
  REQUIRE(data_event_config.t_enable == 1U);

  scmi_tlm_de_info data_event_info{};
  REQUIRE(assigned_interface.GetDataEventInfo(0x5678U, data_event_info) == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE(data_event_info.id == 0x5678U);

  scmi_tlm_de_sample sample{};
  REQUIRE(assigned_interface.ReadDataEventValue(0x9ABCU, sample) == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE(sample.id == 0x9ABCU);

  std::array<scmi_tlm_de_sample, 1> samples{};
  uint32_t                          sample_count = 1;
  REQUIRE(assigned_interface.ReadSingle(samples, sample_count) == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE(sample_count == 0);
  REQUIRE(assigned_interface.Reset() == ASTL_STATUS_NOT_SUPPORTED);

  auto target_available = astl::ScmiIoctlTargetAvailable(device_path);
  REQUIRE_FALSE(target_available.has_value());
  REQUIRE(target_available.error() == ASTL_STATUS_NOT_SUPPORTED);

  auto data_event_exists = astl::ScmiIoctlDataEventExists(device_path, 0x1234U);
  REQUIRE_FALSE(data_event_exists.has_value());
  REQUIRE(data_event_exists.error() == ASTL_STATUS_NOT_SUPPORTED);

  assigned_interface.Close();
  REQUIRE_FALSE(assigned_interface.IsOpen());
#else
  REQUIRE(ioctl_interface.Open() == ASTL_STATUS_NOT_SUPPORTED);
  REQUIRE_FALSE(ioctl_interface.IsOpen());
#endif
}

TEST_CASE("ScmiIoctlInterface maps directory open failures to file errors", "[scmi_ioctl_interface]") {
  const fs::path  base_path = fs::temp_directory_path() / "astl_directory_scmi_ioctl_interface_test";
  TempFileGuard   cleanup(base_path);
  std::error_code ec;
  fs::create_directories(base_path, ec);
  REQUIRE_FALSE(ec);

  astl::ScmiIoctlInterface ioctl_interface{base_path};

#if defined(__linux__)
  REQUIRE(ioctl_interface.Open() == ASTL_STATUS_FILE_ERROR);
  REQUIRE_FALSE(ioctl_interface.IsOpen());
#else
  REQUIRE(ioctl_interface.Open() == ASTL_STATUS_NOT_SUPPORTED);
#endif
}
