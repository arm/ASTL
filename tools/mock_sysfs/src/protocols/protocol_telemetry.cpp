#include "protocol_telemetry.hpp"

#include <iostream>
#include <string>
#include <vector>

#include "common.hpp"
#include "fsnode.hpp"
#include "protocol_type.hpp"

namespace mock_sysfs {

// TODO(ASCT-149): Dynamically build file tree from schema
std::unique_ptr<FileSystemNode> InitProtocolTelemetry(FileSystemNode *g_root) {
  // Create the telemetry directory under g_root.
  auto telemetry = FileSystemNode::CreateDirectory("scmi_telemetry", g_root, ProtocolType::SCMI_TELEMETRY);

  if (!telemetry) {
    std::cerr << "Error: telemetry is null!" << std::endl;
    abort();
  }

  // Create top-level files under "scmi_telemetry".
  telemetry->AddChild(FileSystemNode::CreateFile("all_des_enable", "0", FileAccess::WRITE_ONLY, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("all_des_tstamp_enable", "0", FileAccess::WRITE_ONLY, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("current_update_interval_ms", "1000", FileAccess::READ_WRITE,
                                                 telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("des_bulk_read", "0\n", FileAccess::READ_ONLY, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("tlm_enable", "1", FileAccess::READ_WRITE, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("version", "1.0.0", FileAccess::READ_ONLY, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  auto des_dir = FileSystemNode::CreateDirectory("des", telemetry.get(), ProtocolType::SCMI_TELEMETRY);

  // TODO(ASCT-148): Dynamically add data events from config vector.
  const std::vector<std::string> data_events = {"0x0000", "0x0016", "0x1010", "0xA000", "0xA001",
                                                "0xA002", "0xA005", "0xA007", "0xA008", "0xA00A"};

  // For each data event, create its event directory and add files.
  for (size_t i = 0; i < data_events.size(); i++) {
    auto event_dir = FileSystemNode::CreateDirectory(data_events[i], des_dir.get(), ProtocolType::SCMI_TELEMETRY);

    event_dir->AddChild(FileSystemNode::CreateFile("enable", "0\n", FileAccess::READ_WRITE, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("tstamp_enable", "0\n", FileAccess::READ_WRITE, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("value", "0\n", FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    // Create the "info" subdirectory.
    auto info_dir = FileSystemNode::CreateDirectory("info", event_dir.get(), ProtocolType::SCMI_TELEMETRY);

    std::string event_info_content =
        "Event ID: " + data_events[i] + "\n" + "Description: Mock event " + std::to_string(i) + "\n";

    info_dir->AddChild(FileSystemNode::CreateFile("compo_instance_id", event_info_content, FileAccess::READ_ONLY,
                                                  info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("compo_type", event_info_content, FileAccess::READ_ONLY,
                                                  info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("instance_id", event_info_content, FileAccess::READ_ONLY,
                                                  info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("persistent", event_info_content, FileAccess::READ_ONLY,
                                                  info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("tstamp_exp", event_info_content, FileAccess::READ_ONLY,
                                                  info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("type", event_info_content, FileAccess::READ_ONLY, info_dir.get(),
                                                  ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("unit", event_info_content, FileAccess::READ_ONLY, info_dir.get(),
                                                  ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("unit_exp", event_info_content, FileAccess::READ_ONLY, info_dir.get(),
                                                  ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(std::move(info_dir));
    des_dir->AddChild(std::move(event_dir));
  }

  telemetry->AddChild(std::move(des_dir));
  return telemetry;
}

// TODO(ASCT-148): Implement handling (e.g. enable/disable, read events, etc.)
ErrorCode HandleProtocolTelemetry(const FileSystemNode *node) {
  std::cout << "protocol_telemetry_handle: " << node->GetName() << std::endl;
  return ErrorCode::SUCCESS;
}

}  // namespace mock_sysfs
