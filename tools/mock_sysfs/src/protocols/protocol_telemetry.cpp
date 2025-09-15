#include "protocol_telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "config_protocol_telemetry.hpp"
#include "fsnode.hpp"
#include "protocol_type.hpp"

namespace mock_sysfs {

std::unique_ptr<FileSystemNode> BuildProtocolTelemetryFileTree(FileSystemNode* g_root);

template <typename ToDuration = std::chrono::milliseconds, typename Rep, typename Period>
inline uint64_t ToRaw(const std::chrono::duration<Rep, Period>& duration) {
  return std::chrono::duration_cast<ToDuration>(duration).count();
}

template <typename ToDuration = std::chrono::milliseconds, typename Clock, typename Duration>
inline uint64_t ToRaw(const std::chrono::time_point<Clock, Duration>& timepoint) {
  return std::chrono::duration_cast<ToDuration>(timepoint.time_since_epoch()).count();
}

template <typename ToDuration = std::chrono::milliseconds, typename T>
inline std::string ToRawString(const T& value) {
  return std::to_string(ToRaw<ToDuration>(value));
}

inline void UpdateEventByInterval(DataEvent* event, std::chrono::system_clock::time_point now,
                                  std::chrono::milliseconds interval) {
  auto elapsed = now - event->last_timestamp_;
  if (elapsed < interval) {
    return;
  }

  auto ticks = ToRaw(elapsed) / ToRaw(interval);

  for (uint64_t i = 0; i < ticks; ++i) {
    event->last_value_ = event->Generate();
  }
}

std::unique_ptr<FileSystemNode> InitProtocolTelemetry(FileSystemNode* g_root) {
  auto& context = SCMITelemetryContext::Instance();

  for (const auto& event : context.GetDataEvents()) {
    event->Generate();
  }

  return BuildProtocolTelemetryFileTree(g_root);
};

// TODO(ASCT-149): Dynamically build file tree from schema
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::unique_ptr<FileSystemNode> BuildProtocolTelemetryFileTree(FileSystemNode* g_root) {
  auto& context = SCMITelemetryContext::Instance();

  // Create the telemetry directory under g_root.
  auto telemetry = FileSystemNode::CreateDirectory("scmi_telemetry", g_root, ProtocolType::SCMI_TELEMETRY);

  if (!telemetry) {
    std::cerr << "Error: telemetry is null!" << '\n';
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
      FileSystemNode::CreateFile("current_update_interval_ms", ToRawString(context.GetCurrentUpdateIntervalMs()),
                                 FileAccess::READ_WRITE, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("des_bulk_read", "", FileAccess::READ_ONLY, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(
      FileSystemNode::CreateFile("tlm_enable", std::to_string(static_cast<int>(context.GetTlmEnableFlag())),
                                 FileAccess::READ_WRITE, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("version", context.GetVersion(), FileAccess::READ_ONLY,
                                                 telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("de_implementation_version", context.GetDEImplementationVersion(),
                                                 FileAccess::READ_ONLY, telemetry.get(), ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("des_single_sample_read", "", FileAccess::READ_ONLY, telemetry.get(),
                                                 ProtocolType::SCMI_TELEMETRY));

  auto des_dir = FileSystemNode::CreateDirectory("des", telemetry.get(), ProtocolType::SCMI_TELEMETRY);

  std::string initial_value;
  if (context.GetIntervalsAreDiscreteFlag()) {
    const auto& intervals = context.GetAvailableUpdateIntervalsMs();
    for (auto interval : intervals) {
      initial_value += ToRawString(interval) + " ";
    }
    if (!initial_value.empty()) {
      initial_value.pop_back();  // Remove trailing space
    }
  } else {
    initial_value = ToRawString(context.GetLowestInterval()) + " " + ToRawString(context.GetHighestInterval()) + " " +
                    std::to_string(context.GetStepSize());
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
        "value", std::format("{} {:016x}\n", ToRaw(data_event->last_timestamp_), data_event->last_value_.ui64),
        FileAccess::READ_ONLY, event_dir.get(), ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("compo_instance_id", std::to_string(data_event->compo_instance_id_),
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("compo_type", std::to_string(data_event->compo_type_),
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("instance_id", std::to_string(data_event->instance_id_),
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("persistent", data_event->persistent_ ? "1\n" : "0\n",
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("tstamp_exp", data_event->tstamp_exp_ ? "1\n" : "0\n",
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("type", std::to_string(data_event->type_), FileAccess::READ_ONLY,
                                                   event_dir.get(), ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("unit", data_event->unit_, FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("unit_exp", data_event->unit_exp_, FileAccess::READ_ONLY,
                                                   event_dir.get(), ProtocolType::SCMI_TELEMETRY));

    des_dir->AddChild(std::move(event_dir));
  }

  // Groups
  auto groups_dir = FileSystemNode::CreateDirectory("groups", telemetry.get(), ProtocolType::SCMI_TELEMETRY);
  for (const auto& [group_id, group] : context.GetGroups()) {
    // directory for group n, where "n" is the numeric group ID used
    // as the directory name (e.g., groups/0, groups/1, ...).
    auto group_n_dir =
        FileSystemNode::CreateDirectory(std::to_string(group_id), groups_dir.get(), ProtocolType::SCMI_TELEMETRY);

    // group specific files
    auto composing_des = FileSystemNode::CreateFile("composing_des", "", FileAccess::READ_ONLY, group_n_dir.get(),
                                                    ProtocolType::SCMI_TELEMETRY);

    group_n_dir->AddChild(FileSystemNode::CreateFile(
        "current_update_interval_ms", ToRawString(group->intervals.active_update_interval_ms), FileAccess::READ_WRITE,
        group_n_dir.get(), ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("des_bulk_read", "", FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("des_single_sample_read", "", FileAccess::READ_ONLY,
                                                     group_n_dir.get(), ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("enable", group->enable ? "1" : "0", FileAccess::READ_WRITE,
                                                     group_n_dir.get(), ProtocolType::SCMI_TELEMETRY));

    initial_value.clear();
    if (group->intervals.discrete) {
      const auto& intervals = group->intervals.update_intervals_ms;
      for (auto interval : intervals) {
        initial_value += ToRawString(interval) + " ";
      }
      if (!initial_value.empty()) {
        initial_value.pop_back();  // Remove trailing space
      }
    } else {
      auto min_it =
          std::min_element(group->intervals.update_intervals_ms.begin(), group->intervals.update_intervals_ms.end());
      auto max_it =
          std::max_element(group->intervals.update_intervals_ms.begin(), group->intervals.update_intervals_ms.end());
      initial_value = std::to_string(min_it->count()) + " " + std::to_string(max_it->count()) + " " +
                      std::to_string(context.GetStepSize());
    }

    group_n_dir->AddChild(FileSystemNode::CreateFile("available_update_intervals_ms", initial_value,
                                                     FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("intervals_discrete", group->intervals.discrete ? "1" : "0",
                                                     FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("tstamp_enable", group->tstamp_enable ? "1" : "0",
                                                     FileAccess::READ_WRITE, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    // de specific changes
    for (auto event : group->des) {
      auto& file_content = composing_des->GetFileContent();
      file_content += std::format("0x{:04X} ", event);
    }

    if (!composing_des->GetFileContent().empty()) {
      composing_des->GetFileContent().pop_back();  // Remove trailing space
    }

    group_n_dir->AddChild(std::move(composing_des));

    groups_dir->AddChild(std::move(group_n_dir));
  };

  telemetry->AddChild(std::move(des_dir));
  telemetry->AddChild(std::move(groups_dir));
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
  if (name == "des_single_sample_read") {
    return TelemetryFile::DES_SINGLE_SAMPLE_READ;
  }
  if (name == "tlm_enable") {
    return TelemetryFile::TLM_ENABLE;
  }
  if (name == "version") {
    return TelemetryFile::VERSION;
  }
  if (name == "de_implementation_version") {
    return TelemetryFile::DE_IMPLEMENTATION_VERSION;
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
  if (name == "composing_des") {
    return TelemetryFile::COMPOSING_DES;
  }

  return TelemetryFile::UNKNOWN;
}

SCMITelemetryContext::SCMITelemetryContext(bool all_des_enable, bool all_des_tstamp_enable, UpdateInterval intervals,
                                           bool tlm_enable, std::string version, std::string de_implementation_version,
                                           std::vector<std::unique_ptr<DataEvent>> data_events)
    : all_des_enable_(all_des_enable),
      all_des_tstamp_enable_(all_des_tstamp_enable),
      intervals_(std::move(intervals)),
      tlm_enable_(tlm_enable),
      version_(std::move(version)),
      de_implementation_version_(std::move(de_implementation_version)),
      data_events_(std::move(data_events)) {
  for (const auto& event : data_events_) {
    if (!event->group_.has_value()) {
      continue;
    }

    const DesGroup* src        = event->group_.value();
    auto [groups_it, inserted] = groups_.try_emplace(src->group_id, std::make_unique<DesGroup>(*src));

    event->group_ = groups_it->second.get();
  }
}

SCMITelemetryContext& SCMITelemetryContext::Instance() {
  static SCMITelemetryContext instance(kAllDesEnable, kAllDesTstampEnable, kUpdateInterval, kTlmEnable,
                                       kTelemetryVersion, kDEDataEventVersion, CreateTelemetryDataEvents());
  return instance;
}

DataEvent* SCMITelemetryContext::GetDataEventById(uint16_t identifier) {
  auto       id_matches = [identifier](const auto& event) { return event->id_ == identifier; };
  const auto it         = std::find_if(data_events_.begin(), data_events_.end(), id_matches);
  return (it != data_events_.end()) ? it->get() : nullptr;
}

// TODO(ASTL-315): refactor this switch into a dispatch table
// with dedicated handler functions (e.g. HandleWriteAllDesEnable).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
ErrorCode HandleProtocolTelemetryWrite(const FileSystemNode* node, const std::string& value) {
  auto&         context   = SCMITelemetryContext::Instance();
  std::string   file_name = node->GetName();
  TelemetryFile file_type = GetTelemetryFile(file_name);

  const FileSystemNode*   parent       = node->GetParent();
  const FileSystemNode*   grand_parent = parent ? parent->GetParent() : nullptr;
  bool                    is_group     = ((grand_parent != nullptr) && grand_parent->GetName() == "groups");
  std::optional<uint32_t> group_id;

  if (is_group) {
    group_id = static_cast<uint32_t>(std::stoul(parent->GetName()));
  }

  switch (file_type) {
    case TelemetryFile::ALL_DES_ENABLE:
      if (is_group) {
        auto& group = context.GetGroups().at(group_id.value());
        for (const auto& event : context.GetDataEvents()) {
          if (event->group_.has_value() && event->group_.value()->group_id == group_id.value()) {
            event->enable_ = (std::atoi(value.c_str()) != 0);
          }
        }
        return ErrorCode::SUCCESS;
      }

      context.SetAllDesEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set all_des_enable to " << (std::atoi(value.c_str()) == 1 ? "2" : "0") << '\n';

      for (const auto& event : context.GetDataEvents()) {
        event->enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;

    case TelemetryFile::ALL_DES_TSTAMP_ENABLE:
      context.SetAllDesTstampEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set all_des_tstamp_enable to " << (std::atoi(value.c_str()) == 1 ? "1" : "0") << '\n';

      for (const auto& event : context.GetDataEvents()) {
        event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;

    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS: {
      std::chrono::milliseconds interval{std::stoi(value)};

      if (is_group) {
        const auto& group               = context.GetGroups().at(group_id.value());
        const auto& available_intervals = group->intervals.update_intervals_ms;

        // TODO(danngu01): implement check for if not discrete intervals
        if (std::find(available_intervals.begin(), available_intervals.end(), interval) != available_intervals.end()) {
          group->intervals.active_update_interval_ms = interval;
          std::cout << "Set group current_update_interval_ms to " << interval << "\n";
          return ErrorCode::SUCCESS;
        }
      } else {
        const auto& available_intervals = context.GetAvailableUpdateIntervalsMs();

        // TODO(danngu01): implement check for if not discrete intervals
        if (std::find(available_intervals.begin(), available_intervals.end(), interval) != available_intervals.end()) {
          context.SetCurrentUpdateIntervalMs(interval);
          std::cout << "Set current_update_interval_ms to " << interval << "\n";
          return ErrorCode::SUCCESS;
        }
      }

      std::cerr << "Unsupported update interval: " << interval << '\n';
      return ErrorCode::UNSUPPORTED_PROTOCOL;
    }

    case TelemetryFile::TLM_ENABLE:
      context.SetTlmEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set tlm_enable to " << (std::atoi(value.c_str()) == 1 ? "1" : "0") << '\n';
      return ErrorCode::SUCCESS;

    // DE Handler
    case TelemetryFile::ENABLE: {
      if (is_group) {
        for (const auto& event : context.GetDataEvents()) {
          if (event->group_.has_value() && event->group_.value()->group_id == group_id.value()) {
            event->enable_ = (std::atoi(value.c_str()) != 0);
          }
        }
      } else {
        uint16_t data_event_id = static_cast<uint16_t>(std::stoul(parent->GetName(), nullptr, kHexRadix));
        if (auto* event = context.GetDataEventById(data_event_id)) {
          event->enable_ = (std::atoi(value.c_str()) != 0);
        }
      }

      return ErrorCode::SUCCESS;
    }

    case TelemetryFile::TSTAMP_ENABLE: {
      if (is_group) {
        for (const auto& event : context.GetDataEvents()) {
          if (event->group_.has_value() && event->group_.value()->group_id == group_id.value()) {
            event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);
          }
        }
      } else {
        uint16_t data_event_id = static_cast<uint16_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
        std::cout << "id is: " << data_event_id << "\n";
        DataEvent* data_event = context.GetDataEventById(data_event_id);

        data_event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;
    }

    default:
      std::cout << "Unknown telemetry file: " << file_name << '\n';
      return ErrorCode::UNSUPPORTED_PROTOCOL;
  }
}

// TODO(ASTL-315): refactor this switch into a dispatch table
// with dedicated handler functions (e.g. HandleReadAllDesEnable).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string HandleProtocolTelemetryRead(const FileSystemNode* node) {
  auto&         context   = SCMITelemetryContext::Instance();
  std::string   file_name = node->GetName();
  TelemetryFile file_type = GetTelemetryFile(file_name);

  const FileSystemNode*   parent       = node->GetParent();
  const FileSystemNode*   grand_parent = parent ? parent->GetParent() : nullptr;
  bool                    is_group     = ((grand_parent != nullptr) && grand_parent->GetName() == "groups");
  std::optional<uint32_t> group_id;

  if (is_group) {
    group_id = static_cast<uint32_t>(std::stoul(parent->GetName()));
  }

  switch (file_type) {
      // case TelemetryFile::ALL_DES_ENABLE:
      //   return context.GetAllDesEnable() ? "1" : "0";

      // case TelemetryFile::ALL_DES_TSTAMP_ENABLE:
      //   return context.GetAllDesTstampEnable() ? "1" : "0";

    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS:
      if (is_group) {
        const auto& group = context.GetGroups().at(group_id.value());
        return ToRawString(group->intervals.active_update_interval_ms);
      }
      return ToRawString(context.GetCurrentUpdateIntervalMs());

    case TelemetryFile::DES_BULK_READ: {
      // construct vector of raw data events because groups will return raw pointers, while data events will return
      // unique_ptrs
      std::vector<DataEvent*> events_list;
      for (const auto& event : context.GetDataEvents()) {
        if (!is_group || (event->group_.has_value() && event->group_.value()->group_id == group_id.value())) {
          events_list.push_back(event.get());
        }
      }

      auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

      // Update interval in milliseconds.
      auto interval_ms = is_group ? context.GetGroups().at(group_id.value())->intervals.active_update_interval_ms
                                  : context.GetCurrentUpdateIntervalMs();

      std::string result;

      for (auto* event : events_list) {
        if (!event || !event->enable_ || !context.GetTlmEnableFlag()) {
          continue;
        }

        UpdateEventByInterval(event, now_ms, interval_ms);

        // Append latest timestamp if enabled, else 0
        result += event->tstamp_enable_ ? ToRawString(event->last_timestamp_.time_since_epoch()) + " " : "0 ";
        result += std::format("0x{:04x} ", static_cast<unsigned int>(event->id_));

        // TODO(ASTL-116) - Handle all other _astl_value_type_t
        result += std::format("{:016x} \n", event->last_value_.ui64);
      }

      return result;
    }

    case TelemetryFile::INTERVALS_DISCRETE: {
      if (is_group) {
        const auto& group     = context.GetGroups().at(group_id.value());
        const auto& intervals = group->intervals.update_intervals_ms;

        if (intervals.empty()) {
          return "";
        }

        auto min_it = std::min_element(intervals.begin(), intervals.end());
        auto max_it = std::max_element(intervals.begin(), intervals.end());

        return std::to_string(min_it->count()) + " " + std::to_string(max_it->count()) + " " +
               std::to_string(group->intervals.step_size) + "\n";
      }

      if (context.GetIntervalsAreDiscreteFlag()) {
        return "0\n";
      }

      return ToRawString(context.GetLowestInterval()) + " " + ToRawString(context.GetHighestInterval()) + " " +
             std::to_string(context.GetStepSize()) + "\n";
    }

    case TelemetryFile::TLM_ENABLE:
      return context.GetTlmEnableFlag() ? "1" : "0";

    // DE Handler
    case TelemetryFile::ENABLE: {
      if (is_group) {
        const auto& groups = context.GetGroups().at(group_id.value());
        return groups->enable ? "1" : "0";
      }

      uint16_t data_event_id = static_cast<uint16_t>(std::stoul(parent->GetName(), nullptr, kHexRadix));
      if (auto* event = context.GetDataEventById(data_event_id)) {
        return event->enable_ ? "1" : "0";
      }

      return "";
    }

    case TelemetryFile::TSTAMP_ENABLE: {
      if (is_group) {
        const auto& groups = context.GetGroups().at(group_id.value());
        return groups->tstamp_enable ? "1" : "0";
      }

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

      auto now      = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
      auto interval = context.GetCurrentUpdateIntervalMs();  // std::chrono::milliseconds

      UpdateEventByInterval(data_event, now, interval);

      // TODO(ASTL-116) - Handle all other _astl_value_type_t
      if (data_event->tstamp_enable_) {
        return std::format("{} {:016x}\n", ToRaw(now.time_since_epoch()), data_event->last_value_.ui64);
      }
      return std::format("0 {:016x}\n", data_event->last_value_.ui64);
    }

    case TelemetryFile::DES_SINGLE_SAMPLE_READ: {
      // construct vector of raw data events because groups will return raw pointers, while data events will return
      // unique_ptrs
      std::vector<DataEvent*> events_list;
      for (const auto& event : context.GetDataEvents()) {
        if (!is_group || (event->group_ && event->group_.value()->group_id == group_id.value())) {
          events_list.push_back(event.get());
        }
      }

      auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

      std::string result;

      for (auto* event : events_list) {
        if (!event || !event->enable_ || !context.GetTlmEnableFlag()) {
          continue;
        }

        event->last_value_ = event->Generate();

        // Append latest timestamp if enabled, else 0
        result += event->tstamp_enable_ ? ToRawString(event->last_timestamp_.time_since_epoch()) + " " : "0 ";
        result += std::format("0x{:04x} ", static_cast<unsigned int>(event->id_));

        // TODO(ASTL-116) - Handle all other _astl_value_type_t
        result += std::format("{:016x} \n", event->last_value_.ui64);
      }

      return result;
    }

    default:
      std::cout << "Unknown telemetry file: " << file_name << '\n';
      return "";
  }
}

}  // namespace mock_sysfs
