#ifndef INCLUDE_PROTOCOL_TELEMETRY_HPP
#define INCLUDE_PROTOCOL_TELEMETRY_HPP

#include "common.hpp"
#include "fsnode.hpp"

namespace mock_sysfs {

/**
 * @brief Initializes the telemetry protocol subtree.
 *
 * @details Creates a "scmi_telemetry" directory under the given root and adds
 * top-level telemetry files. Currently, this creates hard-coded events.
 *
 * TODO(ASCT-148): Discover config vector to dynamically build DE structure.
 *
 * @param g_root Pointer to the root file system node under which the telemetry
 *               directory will be created.
 * @return FileSystemNode Unique Pointer to the newly created telemetry directory node.
 */
std::unique_ptr<FileSystemNode> InitProtocolTelemetry(FileSystemNode* g_root);

/**
 * @brief Handles telemetry protocol operations for a given node.
 *
 * @details Processes SCMI telemetry protocol-specific commands or events for
 * the specified file system node.
 *
 * @param node Pointer to the file system node associated with telemetry operations.
 * @return int Status code indicating success (typically 0) or an error value.
 */
ErrorCode HandleProtocolTelemetry(const FileSystemNode* node);

}  // namespace mock_sysfs

#endif  // INCLUDE_PROTOCOL_TELEMETRY_HPP
