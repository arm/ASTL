#ifndef INCLUDE_PROTOCOL_HPP
#define INCLUDE_PROTOCOL_HPP

#include "common.hpp"
#include "fsnode.hpp"

namespace mock_sysfs {

/**
 * @brief Initializes SCMI protocol handlers for the file system.
 *
 * @details Currently initializes the telemetry protocol handler by creating its
 * subtree under the provided root node. This function is extensible to initialize
 * additional protocols.
 *
 * @param g_root Pointer to the root file system node.
 */
void InitProtocol(FileSystemNode* g_root);

/**
 * @brief Dispatches handling of a file system node based on its protocol.
 *
 * @details Inspects the protocol type of the given node and calls the
 * corresponding protocol-specific handler. If the node's protocol is SCMI telemetry,
 * it delegates to @ref ProtocolTelemetryHandle. Additional protocol cases can be added
 * in the switch statement. If no handler exists for the protocol, an error is logged and
 * MOCK_SYSFS_PROTOCOL_UNSUPPPORTED is returned.
 *
 * @param node A pointer to the file system node whose protocol operations are to be handled.
 * @return int Status code from the protocol-specific handler, or MOCK_SYSFS_PROTOCOL_UNSUPPPORTED if no handler is
 * available.
 */
ErrorCode HandleProtocol(FileSystemNode* node);

}  // namespace mock_sysfs

#endif  // INCLUDE_PROTOCOL_HPP
