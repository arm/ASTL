// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "collector/scmi_ioctl_interface.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <utility>

#include "astl_logger.hpp"

#if defined(__linux__)
#  include <fcntl.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace astl {
namespace {

/**
 * @brief Converts errno values from open() and ioctl() into ASTL status codes.
 *
 * @param error_number errno value captured immediately after the failed syscall.
 * @return Closest ASTL status code for the failure.
 */
#if defined(__linux__)
auto ErrnoToStatus(int error_number) -> astl_status_code {
  auto status = ASTL_STATUS_FILE_ERROR;
  switch (error_number) {
    case 0:
      status = ASTL_STATUS_SUCCESS;
      break;
    case ENOENT:
    case ENODEV:
      status = ASTL_STATUS_NO_TARGET_FOUND;
      break;
    case EACCES:
    case EPERM:
      status = ASTL_STATUS_FILE_OPEN_FAILED;
      break;
    case EBUSY:
      status = ASTL_STATUS_COLLECTION_ALREADY_RUNNING;
      break;
    case EINVAL:
      status = ASTL_STATUS_BAD_ARGUMENT;
      break;
    case ENOMEM:
      status = ASTL_STATUS_OUT_OF_MEMORY;
      break;
    case ENOTTY:
      status = ASTL_STATUS_NOT_SUPPORTED;
      break;
    default:
      break;
  }
  return status;
}
#endif

/**
 * @brief Checks whether a string is non-empty and contains only decimal digits.
 *
 * @param value String view to inspect.
 * @return true when value contains only decimal digits.
 */
auto IsAllDigits(std::string_view value) -> bool {
  return !value.empty() &&
         std::ranges::all_of(value, [](unsigned char character) { return std::isdigit(character) != 0; });
}

}  // namespace

/**
 * @brief Creates an unopened interface for a specific SCMI telemetry device.
 *
 * @param device_path Path to a telemetry character device, such as `/dev/scmi/tlm_0`.
 */
ScmiIoctlInterface::ScmiIoctlInterface(std::filesystem::path device_path) : _device_path{std::move(device_path)} {}

/**
 * @brief Closes the device descriptor if it is open.
 */
ScmiIoctlInterface::~ScmiIoctlInterface() { Close(); }

/**
 * @brief Moves an interface and transfers any open file descriptor.
 *
 * The destination is newly constructed, so it has no existing descriptor to close.
 *
 * @param other Interface to move from.
 */
ScmiIoctlInterface::ScmiIoctlInterface(ScmiIoctlInterface&& other) noexcept
    : _device_path{std::move(other._device_path)}, _fd{std::exchange(other._fd, -1)} {}

/**
 * @brief Move-assigns an interface and transfers any open file descriptor.
 *
 * Any descriptor already owned by the destination is closed before the transfer.
 *
 * @param other Interface to move from.
 * @return Reference to this interface.
 */
auto ScmiIoctlInterface::operator=(ScmiIoctlInterface&& other) noexcept -> ScmiIoctlInterface& {
  if (this == &other) {
    return *this;
  }
  Close();
  _device_path = std::move(other._device_path);
  _fd          = std::exchange(other._fd, -1);
  return *this;
}

/**
 * @brief Opens the SCMI telemetry device if it is not already open.
 *
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from errno.
 */
auto ScmiIoctlInterface::Open() -> astl_status_code {
#if defined(__linux__)
  if (IsOpen()) {
    return ASTL_STATUS_SUCCESS;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open is the required kernel device API.
  _fd = open(_device_path.c_str(), O_RDWR | O_CLOEXEC);
  if (_fd < 0) {
    const int error_number = errno;
    ASTL_LOG_DEBUG("Failed to open SCMI ioctl device '{}': {}", _device_path.string(), std::strerror(error_number));
    return ErrnoToStatus(error_number);
  }
  return ASTL_STATUS_SUCCESS;
#else
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Closes the SCMI telemetry device if it is open.
 */
auto ScmiIoctlInterface::Close() noexcept -> void {
#if defined(__linux__)
  if (_fd >= 0) {
    close(_fd);
    _fd = -1;
  }
#endif
}

/**
 * @brief Checks whether the underlying device descriptor is open.
 *
 * @return true when a device descriptor is currently open.
 */
auto ScmiIoctlInterface::IsOpen() const noexcept -> bool { return _fd >= 0; }

/**
 * @brief Returns the configured SCMI telemetry device path.
 *
 * @return Reference to the device path owned by this interface.
 */
auto ScmiIoctlInterface::DevicePath() const -> const std::filesystem::path& { return _device_path; }

/**
 * @brief Opens the device if needed and executes one raw ioctl call.
 *
 * @param request Encoded ioctl request number.
 * @param arg Pointer to the UAPI structure expected by the request.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from errno.
 */
auto ScmiIoctlInterface::Ioctl(std::uint64_t request, void* arg) -> astl_status_code {
#if defined(__linux__)
  auto status = Open();
  if (status != ASTL_STATUS_SUCCESS) {
    return status;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,google-runtime-int): POSIX ioctl requires this ABI type.
  if (ioctl(_fd, static_cast<unsigned long>(request), arg) != 0) {
    const int error_number = errno;
    ASTL_LOG_DEBUG("SCMI ioctl 0x{:X} failed on '{}': {}", request, _device_path.string(), std::strerror(error_number));
    return ErrnoToStatus(error_number);
  }
  return ASTL_STATUS_SUCCESS;
#else
  (void)request;
  (void)arg;
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Reads telemetry protocol information with SCMI_TLM_GET_INFO.
 *
 * @param info Output structure populated by the driver.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
 */
auto ScmiIoctlInterface::GetInfo(scmi_tlm_base_info& info) -> astl_status_code {
  info = {};
#if defined(__linux__)
  return Ioctl(SCMI_TLM_GET_INFO, &info);
#else
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Reads target or group telemetry configuration with SCMI_TLM_GET_CFG.
 *
 * @param config Input/output configuration structure.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
 */
auto ScmiIoctlInterface::GetConfig(scmi_tlm_config& config) -> astl_status_code {
#if defined(__linux__)
  return Ioctl(SCMI_TLM_GET_CFG, &config);
#else
  (void)config;
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Writes target or group telemetry configuration with SCMI_TLM_SET_CFG.
 *
 * @param config Configuration structure to write.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
 */
auto ScmiIoctlInterface::SetConfig(scmi_tlm_config& config) -> astl_status_code {
#if defined(__linux__)
  return Ioctl(SCMI_TLM_SET_CFG, &config);
#else
  (void)config;
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Reads the enable and timestamp-enable state for one data event.
 *
 * @param data_event_id SCMI data event identifier.
 * @param config Output data event configuration.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
 */
auto ScmiIoctlInterface::GetDataEventConfig(ScmiDataEventId data_event_id, scmi_tlm_de_config& config)
    -> astl_status_code {
  config    = {};
  config.id = data_event_id;
#if defined(__linux__)
  return Ioctl(SCMI_TLM_GET_DE_CFG, &config);
#else
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Writes the enable and timestamp-enable state for one data event.
 *
 * @param config Data event configuration to write.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
 */
auto ScmiIoctlInterface::SetDataEventConfig(scmi_tlm_de_config& config) -> astl_status_code {
  config.enable   = config.enable != 0 ? 1U : 0U;
  config.t_enable = config.t_enable != 0 ? 1U : 0U;
#if defined(__linux__)
  return Ioctl(SCMI_TLM_SET_DE_CFG, &config);
#else
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Reads metadata for one data event with SCMI_TLM_GET_DE_INFO.
 *
 * @param data_event_id SCMI data event identifier.
 * @param info Output data event metadata.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
 */
auto ScmiIoctlInterface::GetDataEventInfo(ScmiDataEventId data_event_id, scmi_tlm_de_info& info) -> astl_status_code {
  info    = {};
  info.id = data_event_id;
#if defined(__linux__)
  return Ioctl(SCMI_TLM_GET_DE_INFO, &info);
#else
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Reads one data event value and timestamp with SCMI_TLM_GET_DE_VALUE.
 *
 * @param data_event_id SCMI data event identifier.
 * @param sample Output sample containing the timestamp and value.
 * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
 */
auto ScmiIoctlInterface::ReadDataEventValue(ScmiDataEventId data_event_id, scmi_tlm_de_sample& sample)
    -> astl_status_code {
  sample    = {};
  sample.id = data_event_id;
#if defined(__linux__)
  return Ioctl(SCMI_TLM_GET_DE_VALUE, &sample);
#else
  return ASTL_STATUS_NOT_SUPPORTED;
#endif
}

/**
 * @brief Converts an ASTL telemetry target path to the corresponding ioctl device path.
 *
 * ASTL keeps target metadata in the legacy `tlm-N` spelling, while the kernel
 * character devices use `tlm_N`.
 *
 * @param device_root Directory containing SCMI telemetry character devices.
 * @param telemetry_subdirectory ASTL target path, such as `tlm-0`.
 * @return Full ioctl device path, such as `/dev/scmi/tlm_0`.
 */
auto ScmiIoctlInterface::DevicePathFromTelemetrySubdirectory(const std::filesystem::path& device_root,
                                                             std::string_view             telemetry_subdirectory)
    -> std::filesystem::path {
  std::string device_name{telemetry_subdirectory};
  if (device_name.starts_with("tlm-")) {
    device_name.replace(3, 1, "_");
  }
  return device_root / device_name;
}

/**
 * @brief Converts an ioctl device name to ASTL's stable telemetry target path.
 *
 * @param device_name Ioctl device basename, such as `tlm_0`.
 * @return ASTL telemetry target path, such as `tlm-0`.
 */
auto ScmiIoctlInterface::TelemetrySubdirectoryFromDeviceName(std::string_view device_name) -> std::string {
  std::string telemetry_subdirectory{device_name};
  if (telemetry_subdirectory.starts_with("tlm_")) {
    telemetry_subdirectory.replace(3, 1, "-");
  }
  return telemetry_subdirectory;
}

/**
 * @brief Formats the raw data-event implementation version as a UUID-compatible hex string.
 *
 * @param info Base telemetry information returned by SCMI_TLM_GET_INFO.
 * @return Uppercase hexadecimal data-event implementation identifier.
 */
auto ScmiIoctlInterface::FormatDeImplementationVersion(const scmi_tlm_base_info& info) -> std::string {
  return std::format("{:08X}{:08X}{:08X}{:08X}", info.de_impl_version[0], info.de_impl_version[1],
                     info.de_impl_version[2], info.de_impl_version[3]);
}

/**
 * @brief Checks whether a directory entry name looks like an SCMI telemetry ioctl device.
 *
 * @param device_name Basename to test.
 * @return true for names in the `tlm_<digits>` form.
 */
auto ScmiIoctlInterface::IsLikelyTelemetryDeviceName(std::string_view device_name) -> bool {
  if (!device_name.starts_with("tlm_")) {
    return false;
  }
  return IsAllDigits(device_name.substr(4));
}

/**
 * @brief Checks whether an ioctl telemetry target exposes one data event.
 *
 * @param device_path Path to the SCMI telemetry character device.
 * @param data_event_id SCMI data event identifier.
 * @return true when the data event exists, false when absent, or an ASTL status on unexpected failure.
 */
auto ScmiIoctlDataEventExists(const std::filesystem::path& device_path, ScmiDataEventId data_event_id)
    -> std::expected<bool, astl_status_code> {
  ScmiIoctlInterface ioctl_interface{device_path};
  scmi_tlm_de_info   info{};
  const auto         status = ioctl_interface.GetDataEventInfo(data_event_id, info);
  if (status == ASTL_STATUS_SUCCESS) {
    return true;
  }
  if (status == ASTL_STATUS_BAD_ARGUMENT || status == ASTL_STATUS_NO_TARGET_FOUND) {
    return false;
  }
  return std::unexpected(status);
}

/**
 * @brief Checks whether an ioctl telemetry target can be opened and queried.
 *
 * @param device_path Path to the SCMI telemetry character device.
 * @return true when the target answers SCMI_TLM_GET_INFO, false when unavailable, or an ASTL status on unexpected
 * failure.
 */
auto ScmiIoctlTargetAvailable(const std::filesystem::path& device_path) -> std::expected<bool, astl_status_code> {
  ScmiIoctlInterface ioctl_interface{device_path};
  scmi_tlm_base_info info{};
  const auto         status = ioctl_interface.GetInfo(info);
  if (status == ASTL_STATUS_SUCCESS) {
    return true;
  }
  if (status == ASTL_STATUS_NO_TARGET_FOUND || status == ASTL_STATUS_FILE_OPEN_FAILED) {
    return false;
  }
  return std::unexpected(status);
}

}  // namespace astl
