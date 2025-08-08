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

ErrorCode HandleProtocolWrite(const FileSystemNode* node, const std::string& value) {
  switch (node->GetProtocol()) {
    case ProtocolType::SCMI_TELEMETRY:
      return HandleProtocolTelemetryWrite(node, value);

      // Add additional cases for other protocols as needed:
      // case ProtocolType::ANOTHER_PROTOCOL:
      //     return HandleAnotherProtocolWrite(node, value);

    default:
      std::cerr << "No write handler for protocol." << '\n';
      return ErrorCode::UNSUPPORTED_PROTOCOL;
  }
}

std::string HandleProtocolRead(const FileSystemNode* node) {
  switch (node->GetProtocol()) {
    case ProtocolType::SCMI_TELEMETRY:
      return HandleProtocolTelemetryRead(node);

      // Add additional cases for other protocols as needed:
      // case ProtocolType::ANOTHER_PROTOCOL:
      //     return HandleAnotherProtocolRead(node);

    default:
      std::cerr << "No read handler for protocol." << '\n';
      return "";
  }
}

}  // namespace mock_sysfs
