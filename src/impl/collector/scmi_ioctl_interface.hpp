// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SCMI_IOCTL_INTERFACE_HPP_
#define SCMI_IOCTL_INTERFACE_HPP_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "astl/astl_errors.h"
#include "collector/scmi_ioctl_uapi.hpp"
#include "operation/scmi_read_operation.hpp"

namespace astl {

struct IScmiIoctlInterface {
  virtual ~IScmiIoctlInterface() = default;

  IScmiIoctlInterface()                                      = default;
  IScmiIoctlInterface(const IScmiIoctlInterface&)            = delete;
  IScmiIoctlInterface& operator=(const IScmiIoctlInterface&) = delete;
  IScmiIoctlInterface(IScmiIoctlInterface&&)                 = default;
  IScmiIoctlInterface& operator=(IScmiIoctlInterface&&)      = default;

  virtual auto DevicePath() const -> const std::filesystem::path&                                                = 0;
  virtual auto Probe() -> astl_status_code                                                                       = 0;
  virtual auto DeImplementationVersion() -> std::expected<std::string, astl_status_code>                         = 0;
  virtual auto DataEventCount() -> std::expected<uint32_t, astl_status_code>                                     = 0;
  virtual auto SupportsSingleRead() -> std::expected<bool, astl_status_code>                                     = 0;
  virtual auto GetConfig(scmi_tlm_config& config) -> astl_status_code                                            = 0;
  virtual auto SetConfig(scmi_tlm_config& config) -> astl_status_code                                            = 0;
  virtual auto GetDataEventConfig(ScmiDataEventId data_event_id, scmi_tlm_de_config& config) -> astl_status_code = 0;
  virtual auto SetDataEventConfig(scmi_tlm_de_config& config) -> astl_status_code                                = 0;
  virtual auto GetDataEventInfo(ScmiDataEventId data_event_id, scmi_tlm_de_info& info) -> astl_status_code       = 0;
  virtual auto ReadDataEventValue(ScmiDataEventId data_event_id, scmi_tlm_de_sample& sample) -> astl_status_code = 0;
  virtual auto ReadSingle(std::span<scmi_tlm_de_sample> samples, uint32_t& sample_count) -> astl_status_code     = 0;
  virtual auto Reset() -> astl_status_code                                                                       = 0;
};

/**
 * @brief RAII wrapper around one SCMI telemetry ioctl character device.
 *
 * This class owns the file descriptor for a single `/dev/scmi/tlm_N` device and
 * exposes the subset of the SCMI telemetry ioctl UAPI needed by ASTL discovery,
 * metric availability checks, and collection.
 */
class ScmiIoctlInterface : public IScmiIoctlInterface {
 public:
  /** @brief Creates an unopened interface without an associated device path. */
  ScmiIoctlInterface() = default;

  /**
   * @brief Creates an unopened interface for a specific SCMI telemetry device.
   *
   * @param device_path Path to a telemetry character device, such as `/dev/scmi/tlm_0`.
   */
  explicit ScmiIoctlInterface(std::filesystem::path device_path);

  /** @brief Closes the device descriptor if it is open. */
  ~ScmiIoctlInterface() override;

  ScmiIoctlInterface(const ScmiIoctlInterface&)            = delete;
  ScmiIoctlInterface& operator=(const ScmiIoctlInterface&) = delete;

  /**
   * @brief Moves an interface and transfers any open file descriptor.
   *
   * The destination is newly constructed, so it has no existing descriptor to close.
   *
   * @param other Interface to move from.
   */
  ScmiIoctlInterface(ScmiIoctlInterface&& other) noexcept;

  /**
   * @brief Move-assigns an interface and transfers any open file descriptor.
   *
   * Any descriptor already owned by the destination is closed before the transfer.
   *
   * @param other Interface to move from.
   * @return Reference to this interface.
   */
  ScmiIoctlInterface& operator=(ScmiIoctlInterface&& other) noexcept;

  /**
   * @brief Opens the SCMI telemetry device if it is not already open.
   *
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from errno.
   */
  auto Open() -> astl_status_code;

  /** @brief Closes the SCMI telemetry device if it is open. */
  auto Close() noexcept -> void;

  /**
   * @brief Checks whether the underlying device descriptor is open.
   *
   * @return true when a device descriptor is currently open.
   */
  auto IsOpen() const noexcept -> bool;

  /**
   * @brief Returns the configured SCMI telemetry device path.
   *
   * @return Reference to the device path owned by this interface.
   */
  auto DevicePath() const -> const std::filesystem::path& override;

  /** @brief Negotiates and caches the V8 telemetry ABI information. */
  auto Probe() -> astl_status_code override;

  /** @brief Returns the primary DE implementation UUID as uppercase hexadecimal. */
  auto DeImplementationVersion() -> std::expected<std::string, astl_status_code> override;

  /** @brief Returns the number of data events exposed by this instance. */
  auto DataEventCount() -> std::expected<uint32_t, astl_status_code> override;

  /** @brief Reports whether the instance supports SCMI_TLM_SINGLE_READ. */
  auto SupportsSingleRead() -> std::expected<bool, astl_status_code> override;

  /**
   * @brief Reads target or group telemetry configuration with SCMI_TLM_GET_CFG.
   *
   * @param config Input/output configuration structure.
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
   */
  auto GetConfig(scmi_tlm_config& config) -> astl_status_code override;

  /**
   * @brief Writes target or group telemetry configuration with SCMI_TLM_SET_CFG.
   *
   * @param config Configuration structure to write.
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
   */
  auto SetConfig(scmi_tlm_config& config) -> astl_status_code override;

  /**
   * @brief Reads the enable and timestamp-enable state for one data event.
   *
   * @param data_event_id SCMI data event identifier.
   * @param config Output data event configuration.
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
   */
  auto GetDataEventConfig(ScmiDataEventId data_event_id, scmi_tlm_de_config& config) -> astl_status_code override;

  /**
   * @brief Writes the enable and timestamp-enable state for one data event.
   *
   * @param config Data event configuration to write.
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
   */
  auto SetDataEventConfig(scmi_tlm_de_config& config) -> astl_status_code override;

  /**
   * @brief Reads metadata for one data event with SCMI_TLM_GET_DE_INFO.
   *
   * @param data_event_id SCMI data event identifier.
   * @param info Output data event metadata.
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
   */
  auto GetDataEventInfo(ScmiDataEventId data_event_id, scmi_tlm_de_info& info) -> astl_status_code override;

  /**
   * @brief Reads one data event value and timestamp with SCMI_TLM_DE_READ.
   *
   * @param data_event_id SCMI data event identifier.
   * @param sample Output sample containing the timestamp and value.
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from ioctl failure.
   */
  auto ReadDataEventValue(ScmiDataEventId data_event_id, scmi_tlm_de_sample& sample) -> astl_status_code override;

  /**
   * @brief Triggers a platform-side update and reads all enabled data events.
   *
   * The operation is issued only when SCMI_TLM_SCMI_SUPPORT_SINGLE_SAMPLE was
   * advertised. `sample_count` is set to the number of returned samples.
   */
  auto ReadSingle(std::span<scmi_tlm_de_sample> samples, uint32_t& sample_count) -> astl_status_code override;

  /**
   * @brief Resets telemetry state when both ABI and instance support are advertised.
   */
  auto Reset() -> astl_status_code override;

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
  static auto DevicePathFromTelemetrySubdirectory(const std::filesystem::path& device_root,
                                                  std::string_view telemetry_subdirectory) -> std::filesystem::path;

  /**
   * @brief Converts an ioctl device name to ASTL's stable telemetry target path.
   *
   * @param device_name Ioctl device basename, such as `tlm_0`.
   * @return ASTL telemetry target path, such as `tlm-0`.
   */
  static auto TelemetrySubdirectoryFromDeviceName(std::string_view device_name) -> std::string;

  /**
   * @brief Checks whether a directory entry name looks like an SCMI telemetry ioctl device.
   *
   * @param device_name Basename to test.
   * @return true for names in the `tlm_<digits>` form.
   */
  static auto IsLikelyTelemetryDeviceName(std::string_view device_name) -> bool;

 private:
  /**
   * @brief Opens the device if needed and executes one raw ioctl call.
   *
   * @param request Encoded ioctl request number.
   * @param arg Pointer to the UAPI structure expected by the request.
   * @return ASTL_STATUS_SUCCESS on success, or an ASTL status mapped from errno.
   */
  auto Ioctl(std::uint64_t request, void* arg) -> astl_status_code;

  /**
   * @brief Validates and issues a target or group configuration ioctl.
   *
   * @param request SCMI_TLM_GET_CFG or SCMI_TLM_SET_CFG.
   * @param config Configuration structure passed to the driver.
   * @return ASTL_STATUS_SUCCESS on success, or an ABI, capability, or ioctl failure status.
   */
  auto ConfigIoctl(std::uint64_t request, scmi_tlm_config& config) -> astl_status_code;

  /** @brief Path to the SCMI telemetry character device. */
  std::filesystem::path _device_path;

  /** @brief Open file descriptor for _device_path, or -1 when closed. */
  int _fd{-1};

  /** @brief ABI information cached for the lifetime of an open device descriptor. */
  std::optional<scmi_tlm_abi_info> _abi_info;
};

/**
 * @brief Checks whether an ioctl telemetry target exposes one data event.
 *
 * @param device_path Path to the SCMI telemetry character device.
 * @param data_event_id SCMI data event identifier.
 * @return true when the data event exists, false when it is absent, or an ASTL status on unexpected failure.
 */
auto ScmiIoctlDataEventExists(const std::filesystem::path& device_path, ScmiDataEventId data_event_id)
    -> std::expected<bool, astl_status_code>;

/**
 * @brief Checks whether an ioctl telemetry target can be opened and queried.
 *
 * @param device_path Path to the SCMI telemetry character device.
 * @return true when the target answers SCMI_TLM_GET_ABI_INFO, false when unavailable, or an ASTL status on unexpected
 * failure.
 */
auto ScmiIoctlTargetAvailable(const std::filesystem::path& device_path) -> std::expected<bool, astl_status_code>;

}  // namespace astl

#endif  // SCMI_IOCTL_INTERFACE_HPP_
