#include "protocol_telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "common.hpp"
#include "config_protocol_telemetry.hpp"
#include "fsnode.hpp"
#include "protocol_type.hpp"

namespace mock_sysfs {

std::unique_ptr<FileSystemNode> BuildProtocolTelemetryFileTree(FileSystemNode* g_root);

inline void UpdateEventByInterval(DataEvent* event, uint64_t current_ts, uint64_t interval_ms) {
  // How many ms since we last updated?
  uint64_t elapsed = current_ts - event->latest_timestamp_;

  if (elapsed < interval_ms) {
    // Not enough time yet, nothing to do.
    return;
  }

  // Determine how many complete intervals have passed.
  uint64_t count = elapsed / interval_ms;

  // Advance the generator state for each missed tick.
  for (uint64_t i = 0; i < count; ++i) {
    // TODO(ASTL-116) - Handle all other _astl_value_type_t
    event->latest_value_ = event->Generate();
  }

  event->latest_timestamp_ = current_ts;
}

std::unique_ptr<FileSystemNode> InitProtocolTelemetry(FileSystemNode* g_root) {
  auto& context = SCMITelemetryContext::Instance();

  uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());

  for (const auto& event : context.GetDataEvents()) {
    event->latest_timestamp_ = now_ms;
    event->Generate();
  }

  return BuildProtocolTelemetryFileTree(g_root);
};

// TODO(ASCT-149): Dynamically build file tree from schema
std::unique_ptr<FileSystemNode> BuildProtocolTelemetryFileTree(FileSystemNode* g_root) {
  auto& context = SCMITelemetryContext::Instance();

  // Create the telemetry directory under g_root.
  auto telemetry = FileSystemNode::CreateDirectory("scmi_telemetry", g_root, ProtocolType::SCMI_TELEMETRY);

  if (!telemetry) {
    std::cerr << "Error: telemetry is null!" << std::endl;
    abort();
  }

  // Create top-level files under "scmi_telemetry".
  telemetry->AddChild(
      FileSystemNode::CreateFile("all_des_enable", std::to_string(static_cast<int>(context.GetAllDesEnableFlag())),
                                 FileAccess::WRITE_ONLY, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile(
      "all_des_tstamp_enable", std::to_string(static_cast<int>(context.GetAllDesTstampEnableFlag())),
      FileAccess::WRITE_ONLY, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(
      FileSystemNode::CreateFile("current_update_interval_ms", std::to_string(context.GetCurrentUpdateIntervalMs()),
                                 FileAccess::READ_WRITE, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("des_bulk_read", "0\n", FileAccess::READ_ONLY, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(
      FileSystemNode::CreateFile("tlm_enable", std::to_string(static_cast<int>(context.GetTlmEnableFlag())),
                                 FileAccess::READ_WRITE, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("version", context.GetVersion(), FileAccess::READ_WRITE,
                                                 telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  auto des_dir = FileSystemNode::CreateDirectory("des", telemetry.get(), ProtocolType::SCMI_TELEMETRY);

  std::string initial_value;
  if (context.GetIntervalsAreDiscreteFlag()) {
    const auto& intervals = context.GetAvailableUpdateIntervalsMs();
    for (uint32_t interval : intervals) {
      initial_value += std::to_string(interval) + " ";
    }
    if (!initial_value.empty()) {
      initial_value.pop_back();  // Remove trailing space
    }
  } else {
    initial_value = std::to_string(context.GetLowestInterval()) + " " + std::to_string(context.GetHighestInterval()) +
                    " " + std::to_string(context.GetStepSize());
  }

  telemetry->AddChild(FileSystemNode::CreateFile("available_update_intervals_ms", initial_value, FileAccess::READ_WRITE,
                                                 telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(
      FileSystemNode::CreateFile("intervals_discrete", context.GetIntervalsAreDiscreteFlag() ? "1\n" : "0\n",
                                 FileAccess::READ_WRITE, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  // For each data event, create its event directory and add files.
  for (const auto& data_event : context.GetDataEvents()) {
    std::string id_str = std::format("0x{:04X}", data_event->id_);

    auto event_dir = FileSystemNode::CreateDirectory(id_str, des_dir.get(), ProtocolType::SCMI_TELEMETRY);

    event_dir->AddChild(FileSystemNode::CreateFile("enable", data_event->enable_ ? "1\n" : "0\n",
                                                   FileAccess::READ_WRITE, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("tstamp_enable", data_event->tstamp_enable_ ? "1\n" : "0\n",
                                                   FileAccess::READ_WRITE, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    // TODO(ASTL-116) - Handle all other _astl_value_type_t
    event_dir->AddChild(FileSystemNode::CreateFile(
        // NOLINTBEGIN
        "value", std::format("{} {:016x}\n", data_event->latest_timestamp_, data_event->latest_value_.ui64),
        // NOLINTEND
        FileAccess::READ_ONLY, event_dir.get(), ProtocolType::SCMI_TELEMETRY));

    auto info_dir = FileSystemNode::CreateDirectory("info", event_dir.get(), ProtocolType::SCMI_TELEMETRY);

    info_dir->AddChild(FileSystemNode::CreateFile("compo_instance_id", std::to_string(data_event->compo_instance_id_),
                                                  FileAccess::READ_ONLY, info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("compo_type", std::to_string(data_event->compo_type_),
                                                  FileAccess::READ_ONLY, info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("instance_id", std::to_string(data_event->instance_id_),
                                                  FileAccess::READ_ONLY, info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("persistent", data_event->persistent_ ? "1\n" : "0\n",
                                                  FileAccess::READ_ONLY, info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("tstamp_exp", data_event->tstamp_exp_ ? "1\n" : "0\n",
                                                  FileAccess::READ_ONLY, info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("type", std::to_string(data_event->type_), FileAccess::READ_ONLY,
                                                  info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("unit", data_event->unit_, FileAccess::READ_ONLY, info_dir.get(),
                                                  ProtocolType::SCMI_TELEMETRY));

    info_dir->AddChild(FileSystemNode::CreateFile("unit_exp", data_event->unit_exp_, FileAccess::READ_ONLY,
                                                  info_dir.get(), ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(std::move(info_dir));
    des_dir->AddChild(std::move(event_dir));
  }

  telemetry->AddChild(std::move(des_dir));
  return telemetry;
}

TelemetryFile GetTelemetryFile(const std::string& name) {
  if (name == "all_des_enable") {
    return TelemetryFile::ALL_DES_ENABLE;
  }
  if (name == "all_des_tstamp_enable") {
    return TelemetryFile::ALL_DES_TSTAMP_ENABLE;
  }
  if (name == "current_update_interval_ms") {
    return TelemetryFile::CURRENT_UPDATE_INTERVAL_MS;
  }
  if (name == "des_bulk_read") {
    return TelemetryFile::DES_BULK_READ;
  }
  if (name == "tlm_enable") {
    return TelemetryFile::TLM_ENABLE;
  }
  if (name == "version") {
    return TelemetryFile::VERSION;
  }
  if (name == "available_update_intervals_ms") {
    return TelemetryFile::AVAILABLE_UPDATE_INTERVALS_MS;
  }
  if (name == "intervals_discrete") {
    return TelemetryFile::INTERVALS_DISCRETE;
  }
  if (name == "enable") {
    return TelemetryFile::ENABLE;
  }
  if (name == "compo_instance_id") {
    return TelemetryFile::COMPO_INSTANCE_ID;
  }
  if (name == "compo_type") {
    return TelemetryFile::COMPO_TYPE;
  }
  if (name == "instance_id") {
    return TelemetryFile::INSTANCE_ID;
  }
  if (name == "persistent") {
    return TelemetryFile::PERSISTENT;
  }
  if (name == "tstamp_exp") {
    return TelemetryFile::TSTAMP_EXP;
  }
  if (name == "type") {
    return TelemetryFile::TYPE;
  }
  if (name == "unit") {
    return TelemetryFile::UNIT;
  }
  if (name == "unit_exp") {
    return TelemetryFile::UNIT_EXP;
  }
  if (name == "tstamp_enable") {
    return TelemetryFile::TSTAMP_ENABLE;
  }
  if (name == "value") {
    return TelemetryFile::VALUE;
  }

  return TelemetryFile::UNKNOWN;
}

SCMITelemetryContext::SCMITelemetryContext(bool all_des_enable, bool all_des_tstamp_enable,
                                           uint32_t current_update_interval_ms, bool tlm_enable,
                                           const std::string&           version,
                                           const std::vector<uint32_t>& available_update_intervals_ms,
                                           bool intervals_are_discrete, const std::vector<uint32_t>& discrete_intervals,
                                           uint32_t lowest_interval, uint32_t highest_interval, uint32_t step_size,
                                           std::vector<std::unique_ptr<DataEvent>> data_events)
    : all_des_enable_(all_des_enable),
      all_des_tstamp_enable_(all_des_tstamp_enable),
      current_update_interval_ms_(current_update_interval_ms),
      tlm_enable_(tlm_enable),
      version_(version),
      available_update_intervals_ms_(available_update_intervals_ms),
      intervals_are_discrete_(intervals_are_discrete),
      discrete_intervals_(discrete_intervals),
      lowest_interval_(lowest_interval),
      highest_interval_(highest_interval),
      step_size_(step_size),
      data_events_(std::move(data_events)) {}

SCMITelemetryContext& SCMITelemetryContext::Instance() {
  static SCMITelemetryContext instance(kAllDesEnable, kAllDesTstampEnable, kCurrentUpdateIntervalMs, kTlmEnable,
                                       kTelemetryVersion, kAvailableUpdateIntervals, kIntervalsAreDiscrete,
                                       kDiscreteIntervals, kLowestInterval, kHighestInterval, kStepSize,
                                       CreateTelemetryDataEvents());
  return instance;
}

DataEvent* SCMITelemetryContext::GetDataEventById(uint16_t identifier) {
  auto       id_matches = [identifier](const auto& event) { return event->id_ == identifier; };
  const auto it         = std::find_if(data_events_.begin(), data_events_.end(), id_matches);
  return (it != data_events_.end()) ? it->get() : nullptr;
}

ErrorCode HandleProtocolTelemetryWrite(const FileSystemNode* node, const std::string& value) {
  auto&         context   = SCMITelemetryContext::Instance();
  std::string   file_name = node->GetName();
  TelemetryFile file_type = GetTelemetryFile(file_name);

  switch (file_type) {
    case TelemetryFile::ALL_DES_ENABLE:
      context.SetAllDesEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set all_des_enable to " << (std::atoi(value.c_str()) == 1 ? "1" : "0") << std::endl;

      for (const auto& event : context.GetDataEvents()) {
        event->enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;

    case TelemetryFile::ALL_DES_TSTAMP_ENABLE:
      context.SetAllDesTstampEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set all_des_tstamp_enable to " << (std::atoi(value.c_str()) == 1 ? "1" : "0") << std::endl;

      for (const auto& event : context.GetDataEvents()) {
        event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;

    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS: {
      uint32_t    interval            = static_cast<uint32_t>(std::atoi(value.c_str()));
      const auto& available_intervals = context.GetAvailableUpdateIntervalsMs();

      // TODO(danngu01): implement check for if not discrete intervals
      if (std::find(available_intervals.begin(), available_intervals.end(), interval) != available_intervals.end()) {
        context.SetCurrentUpdateIntervalMs(interval);
        std::cout << "Set current_update_interval_ms to " << interval << std::endl;
        return ErrorCode::SUCCESS;
      }

      std::cerr << "Unsupported update interval: " << interval << std::endl;
      return ErrorCode::UNSUPPORTED_PROTOCOL;
    }

    case TelemetryFile::TLM_ENABLE:
      context.SetTlmEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set tlm_enable to " << (std::atoi(value.c_str()) == 1 ? "1" : "0") << std::endl;
      return ErrorCode::SUCCESS;

    // DE Handler
    case TelemetryFile::ENABLE: {
      uint16_t data_event_id = static_cast<uint16_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
      std::cout << "id is: " << data_event_id << "\n";
      DataEvent* data_event = context.GetDataEventById(data_event_id);

      data_event->enable_ = (std::atoi(value.c_str()) != 0);
      return ErrorCode::SUCCESS;
    }

    case TelemetryFile::TSTAMP_ENABLE: {
      uint16_t data_event_id = static_cast<uint16_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
      std::cout << "id is: " << data_event_id << "\n";
      DataEvent* data_event = context.GetDataEventById(data_event_id);

      data_event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);

      return ErrorCode::SUCCESS;
    }

    default:
      std::cout << "Unknown telemetry file: " << file_name << std::endl;
      return ErrorCode::UNSUPPORTED_PROTOCOL;
  }
}

std::string HandleProtocolTelemetryRead(const FileSystemNode* node) {
  auto&         context   = SCMITelemetryContext::Instance();
  std::string   file_name = node->GetName();
  TelemetryFile file_type = GetTelemetryFile(file_name);

  switch (file_type) {
      // case TelemetryFile::ALL_DES_ENABLE:
      //   return context.GetAllDesEnable() ? "1" : "0";

      // case TelemetryFile::ALL_DES_TSTAMP_ENABLE:
      //   return context.GetAllDesTstampEnable() ? "1" : "0";

    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS:
      return std::to_string(context.GetCurrentUpdateIntervalMs());

    case TelemetryFile::DES_BULK_READ: {
      const auto& data_events = context.GetDataEvents();

      uint64_t now_ms = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count());

      // Update interval in milliseconds.
      uint64_t interval_ms = static_cast<uint64_t>(context.GetCurrentUpdateIntervalMs());

      std::string result;

      for (const auto& event : data_events) {
        if (!event || !event->enable_ || !context.GetTlmEnableFlag()) {
          continue;
        }

        UpdateEventByInterval(event.get(), now_ms, interval_ms);

        // Append latest timestamp if enabled, else 0
        result += event->tstamp_enable_ ? std::to_string(event->latest_timestamp_) + " " : "0 ";
        result += std::format("0x{:04x} ", static_cast<unsigned int>(event->id_));

        // TODO(ASTL-116) - Handle all other _astl_value_type_t
        // NOLINTBEGIN
        result += std::format("{:016x} \n", event->latest_value_.ui64);
        // NOLINTEND
      }

      return result;
    }

    case TelemetryFile::INTERVALS_DISCRETE: {
      if (context.GetIntervalsAreDiscreteFlag()) {
        return "0\n";
      }

      return std::to_string(context.GetLowestInterval()) + " " + std::to_string(context.GetHighestInterval()) + " " +
             std::to_string(context.GetStepSize()) + "\n";
    }

    case TelemetryFile::TLM_ENABLE:
      return context.GetTlmEnableFlag() ? "1" : "0";

    // DE Handler
    case TelemetryFile::ENABLE: {
      uint16_t   data_event_id = static_cast<uint16_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
      DataEvent* data_event    = context.GetDataEventById(data_event_id);

      return std::to_string(static_cast<int>(data_event->enable_));
    }

    case TelemetryFile::TSTAMP_ENABLE: {
      uint16_t   data_event_id = static_cast<uint16_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
      DataEvent* data_event    = context.GetDataEventById(data_event_id);

      return std::to_string(static_cast<int>(data_event->tstamp_enable_));
    }

    case TelemetryFile::VALUE: {
      // Retrieve DE id from the parent's name (hexadecimal conversion)
      uint16_t   data_event_id = static_cast<uint16_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
      DataEvent* data_event    = context.GetDataEventById(data_event_id);

      // If the event is not enabled, report as "0\n".
      if (!data_event->enable_ || !context.GetTlmEnableFlag()) {
        return "0\n";
      }

      auto now_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();

      uint64_t interval_ms = static_cast<uint64_t>(context.GetCurrentUpdateIntervalMs());

      UpdateEventByInterval(data_event, now_ms, interval_ms);

      // TODO(ASTL-116) - Handle all other _astl_value_type_t
      if (data_event->tstamp_enable_) {
        // NOLINTBEGIN
        return std::format("{} {:016x}\n", std::to_string(now_ms), data_event->latest_value_.ui64);
        // NOLINTEND
      }
      // NOLINTBEGIN
      return std::format("0 {:016x}\n", data_event->latest_value_.ui64);
      // NOLINTEND
    }

    default:
      std::cout << "Unknown telemetry file: " << file_name << std::endl;
      return "";
  }
}

}  // namespace mock_sysfs
