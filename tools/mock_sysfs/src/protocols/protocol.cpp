#include "protocol.hpp"

#include <iostream>

#include "common.hpp"
#include "fsnode.hpp"
#include "protocol_telemetry.hpp"  // Contains InitProtocolTelemetry() and HandleProtocolTelemetry()

namespace mock_sysfs {

void InitProtocol(FileSystemNode* g_root) {
  // Initialize additional protocols here
  std::unique_ptr<FileSystemNode> telemetry_tree = InitProtocolTelemetry(g_root);
  g_root->AddChild(std::move(telemetry_tree));
}

ErrorCode HandleProtocol(FileSystemNode* node) {
  switch (node->GetProtocol()) {
    case ProtocolType::SCMI_TELEMETRY:
      return HandleProtocolTelemetry(node);

      // Add additional cases as needed:
      // case ProtocolType::ANOTHER_PROTOCOL:
      //     return protocol_another_handle(node);

    default:
      std::cerr << "No handler for protocol." << std::endl;
      return ErrorCode::UNSUPPORTED_PROTOCOL;
  }
}

}  // namespace mock_sysfs
