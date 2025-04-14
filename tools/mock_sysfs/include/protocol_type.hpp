#ifndef INCLUDE_PROTOCOL_TYPE_HPP
#define INCLUDE_PROTOCOL_TYPE_HPP

namespace mock_sysfs {

/**
 * @brief Specifies the SCMI protocol type associated with a file system node.
 *
 * @details This enum defines the supported SCMI protocols. Currently, it includes:
 * - @c NONE: No protocol.
 * - @c SCMI_TELEMETRY: The SCMI telemetry protocol.
 *
 * The enum is designed to be extensible for future protocols, such as additional SCMI drivers
 * for power management, sensor management, etc.
 */
enum class ProtocolType {
  NONE,
  SCMI_TELEMETRY,
  // Add additional protocols here...
};

}  // namespace mock_sysfs

#endif  // INCLUDE_PROTOCOL_TYPE_HPP
