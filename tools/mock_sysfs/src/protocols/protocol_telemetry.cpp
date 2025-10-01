#include "protocol_telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "config_protocol_telemetry.hpp"
#include "fsnode.hpp"
#include "protocol_type.hpp"

namespace mock_sysfs {

std::unique_ptr<FileSystemNode> BuildProtocolTelemetryFileTree(FileSystemNode*    scmi_telemetry_root,
                                                               std::string const& tlm_id);

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

static std::unordered_map<std::string, std::unique_ptr<SCMITelemetryTarget>>& Instances() {
  static std::unordered_map<std::string, std::unique_ptr<SCMITelemetryTarget>> tlm_to_target;
  return tlm_to_target;
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
  auto& tlm_to_tgt = Instances();

  const char* env = std::getenv("ASTL_MOCKSYSFS_TLM_JSON_PATH");
  if (env == nullptr) {
    throw std::runtime_error("ASTL_MOCKSYSFS_TLM_JSON_PATH environment variable is not set.");
  }

  std::ifstream tlm_json_file(env);
  if (!tlm_json_file) {
    throw std::runtime_error(std::string("Could not open file: ") + env);
  }
  const json tlm_json = json::parse(tlm_json_file);

  std::cout << "tlm schema version: " << tlm_json.at("schema_version") << "\n";

  for (const auto& target : tlm_json.at("targets")) {
    const auto&           settings              = target["settings"];
    const auto&           tlm_id                = static_cast<std::string>(target["instance"]);
    const auto&           all_des_enable        = static_cast<bool>(settings["all_des_enable"]);
    const auto&           all_des_tstamp_enable = static_cast<bool>(settings["all_des_tstamp_enable"]);
    const auto&           tlm_enable            = static_cast<bool>(settings["tlm_enable"]);
    const auto&           telemetry_version     = static_cast<std::string>(settings["telemetry_version"]);
    const auto&           de_version            = static_cast<std::string>(settings["data_event_version"]);
    std::string           raw_data_events_path  = settings.at("data_events_path").get<std::string>();
    std::filesystem::path data_events_path      = std::filesystem::path(env).parent_path() / raw_data_events_path;

    UpdateInterval update_intervals{};
    for (const auto& interval : target["update_intervals_ms"]) {
      update_intervals.update_intervals_ms.emplace_back(std::chrono::milliseconds(interval.get<int64_t>()));
    }
    update_intervals.active_update_interval_ms =
        std::chrono::milliseconds(target["active_update_interval_ms"].get<int64_t>());

    auto tlm = std::make_unique<SCMITelemetryTarget>(tlm_id, all_des_enable, all_des_tstamp_enable, update_intervals,
                                                     tlm_enable, telemetry_version, de_version,
                                                     CreateTelemetryDataEvents(data_events_path, update_intervals));
    tlm_to_tgt.emplace(tlm_id, std::move(tlm));
  }

  auto scmi_telemetry = FileSystemNode::CreateDirectory("scmi_telemetry", g_root, ProtocolType::SCMI_TELEMETRY);

  for (const auto& [cur_tlm_id, tlm] : tlm_to_tgt) {
    auto tlm_file_node = BuildProtocolTelemetryFileTree(scmi_telemetry.get(), cur_tlm_id);
    scmi_telemetry->AddChild(std::move(tlm_file_node));
  }

  return scmi_telemetry;
};

std::string GetInitialIntervalValue(const SCMITelemetryTarget& tlm) {
  std::string initial_value;
  if (tlm.GetIntervalsAreDiscreteFlag()) {
    const auto& intervals = tlm.GetAvailableUpdateIntervalsMs();
    for (auto interval : intervals) {
      initial_value += ToRawString(interval) + " ";
    }
    if (!initial_value.empty()) {
      initial_value.pop_back();  // Remove trailing space
    }
  } else {
    initial_value = ToRawString(tlm.GetLowestInterval()) + " " + ToRawString(tlm.GetHighestInterval()) + " " +
                    std::to_string(tlm.GetStepSize());
  }
  return initial_value;
}

void BuildTelemetryTopLevelFiles(SCMITelemetryTarget& tlm, FileSystemNode* telemetry) {
  telemetry->AddChild(FileSystemNode::CreateFile("all_des_enable",
                                                 std::to_string(static_cast<int>(tlm.GetAllDesEnableFlag())),
                                                 FileAccess::WRITE_ONLY, telemetry, ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("all_des_tstamp_enable",
                                                 std::to_string(static_cast<int>(tlm.GetAllDesTstampEnableFlag())),
                                                 FileAccess::WRITE_ONLY, telemetry, ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("current_update_interval_ms",
                                                 ToRawString(tlm.GetCurrentUpdateIntervalMs()), FileAccess::READ_WRITE,
                                                 telemetry, ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(
      FileSystemNode::CreateFile("des_bulk_read", "", FileAccess::READ_ONLY, telemetry, ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("tlm_enable", std::to_string(static_cast<int>(tlm.GetTlmEnableFlag())),
                                                 FileAccess::READ_WRITE, telemetry, ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("version", tlm.GetVersion(), FileAccess::READ_ONLY, telemetry,
                                                 ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("de_implementation_version", tlm.GetDEImplementationVersion(),
                                                 FileAccess::READ_ONLY, telemetry, ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("des_single_sample_read", "", FileAccess::READ_ONLY, telemetry,
                                                 ProtocolType::SCMI_TELEMETRY));

  std::string initial_value = GetInitialIntervalValue(tlm);
  telemetry->AddChild(FileSystemNode::CreateFile("available_update_intervals_ms", initial_value, FileAccess::READ_WRITE,
                                                 telemetry, ProtocolType::SCMI_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("intervals_discrete",
                                                 tlm.GetIntervalsAreDiscreteFlag() ? "1\n" : "0\n",
                                                 FileAccess::READ_WRITE, telemetry, ProtocolType::SCMI_TELEMETRY));
};

void BuildTelemetryDesFiles(SCMITelemetryTarget& tlm, FileSystemNode* telemetry) {
  auto des_dir = FileSystemNode::CreateDirectory("des", telemetry, ProtocolType::SCMI_TELEMETRY);
  // For each data event, create its event directory and add files.
  for (const auto& data_event : tlm.GetDataEvents()) {
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
  telemetry->AddChild(std::move(des_dir));
}

void BuildTelemetryGroupFiles(SCMITelemetryTarget& tlm, FileSystemNode* telemetry) {
  auto groups_dir = FileSystemNode::CreateDirectory("groups", telemetry, ProtocolType::SCMI_TELEMETRY);
  for (const auto& [group_id, group] : tlm.GetGroups()) {
    // directory for group n, where "n" is the numeric group ID used
    // as the directory name (e.g., groups/0, groups/1, ...).
    auto group_n_dir =
        FileSystemNode::CreateDirectory(std::to_string(group_id), groups_dir.get(), ProtocolType::SCMI_TELEMETRY);

    // group specific files
    auto composing_des = FileSystemNode::CreateFile("composing_des", "", FileAccess::READ_ONLY, group_n_dir.get(),
                                                    ProtocolType::SCMI_TELEMETRY);

    group_n_dir->AddChild(FileSystemNode::CreateFile(
        "current_update_interval_ms", ToRawString(group->intervals_.active_update_interval_ms), FileAccess::READ_WRITE,
        group_n_dir.get(), ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("des_bulk_read", "", FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("des_single_sample_read", "", FileAccess::READ_ONLY,
                                                     group_n_dir.get(), ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("enable", group->enable_ ? "1" : "0", FileAccess::READ_WRITE,
                                                     group_n_dir.get(), ProtocolType::SCMI_TELEMETRY));

    std::string initial_value = GetInitialIntervalValue(tlm);
    group_n_dir->AddChild(FileSystemNode::CreateFile("available_update_intervals_ms", initial_value,
                                                     FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("intervals_discrete", group->intervals_.discrete ? "1" : "0",
                                                     FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("tstamp_enable", group->tstamp_enable_ ? "1" : "0",
                                                     FileAccess::READ_WRITE, group_n_dir.get(),
                                                     ProtocolType::SCMI_TELEMETRY));

    // de specific changes
    for (auto event : group->des_) {
      auto& file_content = composing_des->GetFileContent();
      file_content += std::format("0x{:04X} ", event);
    }

    if (!composing_des->GetFileContent().empty()) {
      composing_des->GetFileContent().pop_back();  // Remove trailing space
    }

    group_n_dir->AddChild(std::move(composing_des));

    groups_dir->AddChild(std::move(group_n_dir));
  };
  telemetry->AddChild(std::move(groups_dir));
};

// TODO(ASTL-13): Dynamically build file tree from schema
std::unique_ptr<FileSystemNode> BuildProtocolTelemetryFileTree(FileSystemNode*    scmi_telemetry_root,
                                                               const std::string& tlm_id) {
  auto& tlm = SCMITelemetryTarget::Instance(tlm_id);

  // Create tlm-N under scmi_telemetry_root.
  auto telemetry = FileSystemNode::CreateDirectory(tlm_id, scmi_telemetry_root, ProtocolType::SCMI_TELEMETRY);

  BuildTelemetryTopLevelFiles(tlm, telemetry.get());
  BuildTelemetryDesFiles(tlm, telemetry.get());
  BuildTelemetryGroupFiles(tlm, telemetry.get());

  return telemetry;
}

TelemetryFile GetTelemetryFile(const std::string& name) {
  static const std::unordered_map<std::string, TelemetryFile> lookup = {
      {"all_des_enable",                TelemetryFile::ALL_DES_ENABLE               },
      {"all_des_tstamp_enable",         TelemetryFile::ALL_DES_TSTAMP_ENABLE        },
      {"current_update_interval_ms",    TelemetryFile::CURRENT_UPDATE_INTERVAL_MS   },
      {"des_bulk_read",                 TelemetryFile::DES_BULK_READ                },
      {"des_single_sample_read",        TelemetryFile::DES_SINGLE_SAMPLE_READ       },
      {"tlm_enable",                    TelemetryFile::TLM_ENABLE                   },
      {"version",                       TelemetryFile::VERSION                      },
      {"de_implementation_version",     TelemetryFile::DE_IMPLEMENTATION_VERSION    },
      {"available_update_intervals_ms", TelemetryFile::AVAILABLE_UPDATE_INTERVALS_MS},
      {"intervals_discrete",            TelemetryFile::INTERVALS_DISCRETE           },
      {"enable",                        TelemetryFile::ENABLE                       },
      {"compo_instance_id",             TelemetryFile::COMPO_INSTANCE_ID            },
      {"compo_type",                    TelemetryFile::COMPO_TYPE                   },
      {"instance_id",                   TelemetryFile::INSTANCE_ID                  },
      {"persistent",                    TelemetryFile::PERSISTENT                   },
      {"tstamp_exp",                    TelemetryFile::TSTAMP_EXP                   },
      {"type",                          TelemetryFile::TYPE                         },
      {"unit",                          TelemetryFile::UNIT                         },
      {"unit_exp",                      TelemetryFile::UNIT_EXP                     },
      {"tstamp_enable",                 TelemetryFile::TSTAMP_ENABLE                },
      {"value",                         TelemetryFile::VALUE                        },
      {"composing_des",                 TelemetryFile::COMPOSING_DES                },
  };

  auto it = lookup.find(name);
  if (it != lookup.end()) {
    return it->second;
  }
  return TelemetryFile::UNKNOWN;
}

SCMITelemetryTarget::SCMITelemetryTarget(std::string const& tlm_id, bool all_des_enable, bool all_des_tstamp_enable,
                                         UpdateInterval intervals, bool tlm_enable, std::string version,
                                         std::string                             de_implementation_version,
                                         std::vector<std::unique_ptr<DataEvent>> data_events)
    : all_des_enable_(all_des_enable),
      all_des_tstamp_enable_(all_des_tstamp_enable),
      intervals_(std::move(intervals)),
      tlm_enable_(tlm_enable),
      version_(std::move(version)),
      de_implementation_version_(std::move(de_implementation_version)),
      data_events_(std::move(data_events)) {
  for (const auto& event : data_events_) {
    if (!event->group_id_.has_value()) {
      continue;
    }

    const group_id_t gid = *event->group_id_;
    auto [it, inserted]  = groups_.try_emplace(gid, nullptr);
    if (inserted || it->second == nullptr) {
      auto group = std::make_unique<DesGroup>(gid, all_des_enable_, all_des_tstamp_enable_, intervals_);
      it->second = std::move(group);
    }
    it->second->des_.push_back(event->id_);
  }
}

SCMITelemetryTarget& SCMITelemetryTarget::Instance(std::string const& tlm_id) {
  auto& instances = Instances();

  // find should never fail. if it does, something else went wrong so crash
  auto it = instances.find(tlm_id);
  return *(it->second);
}

DataEvent* SCMITelemetryTarget::GetDataEventById(data_event_id_t identifier) {
  auto       id_matches = [identifier](const auto& event) { return event->id_ == identifier; };
  const auto it         = std::find_if(data_events_.begin(), data_events_.end(), id_matches);
  return (it != data_events_.end()) ? it->get() : nullptr;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
ErrorCode HandleProtocolTelemetryWrite(const FileSystemNode* node, const std::string& value) {
  // find tlm_id by traversing up file tree until we find a parent named "scmi_telemetry"
  const FileSystemNode* tlm_id = node;
  while (tlm_id->GetParent() && tlm_id->GetParent()->GetName() != "scmi_telemetry") {
    tlm_id = tlm_id->GetParent();
  }

  auto&         tlm       = SCMITelemetryTarget::Instance(tlm_id->GetName());
  std::string   file_name = node->GetName();
  TelemetryFile file_type = GetTelemetryFile(file_name);

  const FileSystemNode*     parent       = node->GetParent();
  const FileSystemNode*     grand_parent = parent ? parent->GetParent() : nullptr;
  bool                      is_group     = ((grand_parent != nullptr) && grand_parent->GetName() == "groups");
  std::optional<group_id_t> group_id;

  if (is_group) {
    group_id = static_cast<group_id_t>(std::stoul(parent->GetName()));
  }

  switch (file_type) {
    case TelemetryFile::ALL_DES_ENABLE:
      if (is_group) {
        auto& group = tlm.GetGroups().at(group_id.value());
        for (const auto& event : tlm.GetDataEvents()) {
          if (event->group_id_.value() == group_id.value()) {
            event->enable_ = (std::atoi(value.c_str()) != 0);
          }
        }
        return ErrorCode::SUCCESS;
      }

      tlm.SetAllDesEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set all_des_enable to " << (std::atoi(value.c_str()) == 1 ? "2" : "0") << '\n';

      for (const auto& event : tlm.GetDataEvents()) {
        event->enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;

    case TelemetryFile::ALL_DES_TSTAMP_ENABLE:
      tlm.SetAllDesTstampEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set all_des_tstamp_enable to " << (std::atoi(value.c_str()) == 1 ? "1" : "0") << '\n';

      for (const auto& event : tlm.GetDataEvents()) {
        event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;

    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS: {
      std::chrono::milliseconds interval{std::stoi(value)};

      if (is_group) {
        const auto& group               = tlm.GetGroups().at(group_id.value());
        const auto& available_intervals = group->intervals_.update_intervals_ms;

        // TODO(danngu01): implement check for if not discrete intervals
        if (std::find(available_intervals.begin(), available_intervals.end(), interval) != available_intervals.end()) {
          group->intervals_.active_update_interval_ms = interval;
          std::cout << "Set group current_update_interval_ms to " << interval << "\n";
          return ErrorCode::SUCCESS;
        }
      } else {
        const auto& available_intervals = tlm.GetAvailableUpdateIntervalsMs();

        // TODO(danngu01): implement check for if not discrete intervals
        if (std::find(available_intervals.begin(), available_intervals.end(), interval) != available_intervals.end()) {
          tlm.SetCurrentUpdateIntervalMs(interval);
          std::cout << "Set current_update_interval_ms to " << interval << "\n";
          return ErrorCode::SUCCESS;
        }
      }

      std::cerr << "Unsupported update interval: " << interval << '\n';
      return ErrorCode::UNSUPPORTED_PROTOCOL;
    }

    case TelemetryFile::TLM_ENABLE:
      tlm.SetTlmEnableFlag(std::atoi(value.c_str()) == 1);
      std::cout << "Set tlm_enable to " << (std::atoi(value.c_str()) == 1 ? "1" : "0") << '\n';
      return ErrorCode::SUCCESS;

    // DE Handler
    case TelemetryFile::ENABLE: {
      if (is_group) {
        for (const auto& event : tlm.GetDataEvents()) {
          if (event->group_id_.value() == group_id.value()) {
            event->enable_ = (std::atoi(value.c_str()) != 0);
          }
        }
      } else {
        data_event_id_t data_event_id = static_cast<data_event_id_t>(std::stoul(parent->GetName(), nullptr, kHexRadix));
        if (auto* event = tlm.GetDataEventById(data_event_id)) {
          event->enable_ = (std::atoi(value.c_str()) != 0);
        }
      }

      return ErrorCode::SUCCESS;
    }

    case TelemetryFile::TSTAMP_ENABLE: {
      if (is_group) {
        for (const auto& event : tlm.GetDataEvents()) {
          if (event->group_id_.value() == group_id.value()) {
            event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);
          }
        }
      } else {
        data_event_id_t data_event_id =
            static_cast<data_event_id_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
        std::cout << "id is: " << data_event_id << "\n";
        DataEvent* data_event = tlm.GetDataEventById(data_event_id);

        data_event->tstamp_enable_ = (std::atoi(value.c_str()) != 0);
      }

      return ErrorCode::SUCCESS;
    }

    default:
      std::cout << "Unknown telemetry file: " << file_name << '\n';
      return ErrorCode::UNSUPPORTED_PROTOCOL;
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string HandleProtocolTelemetryRead(const FileSystemNode* node) {
  const FileSystemNode* tlm_id = node;
  while (tlm_id->GetParent() && tlm_id->GetParent()->GetName() != "scmi_telemetry") {
    tlm_id = tlm_id->GetParent();
  }

  auto&         tlm       = SCMITelemetryTarget::Instance(tlm_id->GetName());
  std::string   file_name = node->GetName();
  TelemetryFile file_type = GetTelemetryFile(file_name);

  const FileSystemNode*     parent       = node->GetParent();
  const FileSystemNode*     grand_parent = parent ? parent->GetParent() : nullptr;
  bool                      is_group     = ((grand_parent != nullptr) && grand_parent->GetName() == "groups");
  std::optional<group_id_t> group_id;

  if (is_group) {
    group_id = static_cast<group_id_t>(std::stoul(parent->GetName()));
  }

  switch (file_type) {
      // case TelemetryFile::ALL_DES_ENABLE:
      //   return tlm.GetAllDesEnable() ? "1" : "0";

      // case TelemetryFile::ALL_DES_TSTAMP_ENABLE:
      //   return tlm.GetAllDesTstampEnable() ? "1" : "0";

    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS:
      if (is_group) {
        const auto& group = tlm.GetGroups().at(group_id.value());
        return ToRawString(group->intervals_.active_update_interval_ms);
      }
      return ToRawString(tlm.GetCurrentUpdateIntervalMs());

    case TelemetryFile::DES_BULK_READ: {
      auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

      auto interval_ms = is_group ? tlm.GetGroups().at(group_id.value())->intervals_.active_update_interval_ms
                                  : tlm.GetCurrentUpdateIntervalMs();

      std::string result;
      for (auto& event : tlm.GetDataEvents()) {  // NOLINT
        if (!event || !event->enable_ || !tlm.GetTlmEnableFlag()) {
          continue;
        }

        if (is_group && (event->group_id_.value() != group_id.value())) {
          continue;
        }

        UpdateEventByInterval(event.get(), now_ms, interval_ms);

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
        const auto& group     = tlm.GetGroups().at(group_id.value());
        const auto& intervals = group->intervals_.update_intervals_ms;

        if (intervals.empty()) {
          return "";
        }

        auto min_it = std::min_element(intervals.begin(), intervals.end());
        auto max_it = std::max_element(intervals.begin(), intervals.end());

        return std::to_string(min_it->count()) + " " + std::to_string(max_it->count()) + " " +
               std::to_string(group->intervals_.step_size) + "\n";
      }

      if (tlm.GetIntervalsAreDiscreteFlag()) {
        return "0\n";
      }

      return ToRawString(tlm.GetLowestInterval()) + " " + ToRawString(tlm.GetHighestInterval()) + " " +
             std::to_string(tlm.GetStepSize()) + "\n";
    }

    case TelemetryFile::TLM_ENABLE:
      return tlm.GetTlmEnableFlag() ? "1" : "0";

    // DE Handler
    case TelemetryFile::ENABLE: {
      if (is_group) {
        const auto& groups = tlm.GetGroups().at(group_id.value());
        return groups->enable_ ? "1" : "0";
      }

      data_event_id_t data_event_id = static_cast<data_event_id_t>(std::stoul(parent->GetName(), nullptr, kHexRadix));
      if (auto* event = tlm.GetDataEventById(data_event_id)) {
        return event->enable_ ? "1" : "0";
      }

      return "";
    }

    case TelemetryFile::TSTAMP_ENABLE: {
      if (is_group) {
        const auto& groups = tlm.GetGroups().at(group_id.value());
        return groups->tstamp_enable_ ? "1" : "0";
      }

      data_event_id_t data_event_id =
          static_cast<data_event_id_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
      DataEvent* data_event = tlm.GetDataEventById(data_event_id);

      return std::to_string(static_cast<int>(data_event->tstamp_enable_));
    }

    case TelemetryFile::VALUE: {
      // Retrieve DE id from the parent's name (hexadecimal conversion)
      data_event_id_t data_event_id =
          static_cast<data_event_id_t>(std::stoul(node->GetParent()->GetName(), nullptr, kHexRadix));
      DataEvent* data_event = tlm.GetDataEventById(data_event_id);

      // If the event is not enabled, report as "0\n".
      if (!data_event->enable_ || !tlm.GetTlmEnableFlag()) {
        return "0\n";
      }

      auto now      = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
      auto interval = tlm.GetCurrentUpdateIntervalMs();  // std::chrono::milliseconds

      UpdateEventByInterval(data_event, now, interval);

      // TODO(ASTL-116) - Handle all other _astl_value_type_t
      if (data_event->tstamp_enable_) {
        return std::format("{} {:016x}\n", ToRaw(now.time_since_epoch()), data_event->last_value_.ui64);
      }
      return std::format("0 {:016x}\n", data_event->last_value_.ui64);
    }

    case TelemetryFile::DES_SINGLE_SAMPLE_READ: {
      auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

      std::string result;

      for (auto& event : tlm.GetDataEvents()) {  // NOLINT
        if (!event || !event->enable_ || !tlm.GetTlmEnableFlag()) {
          continue;
        }

        if (is_group && (event->group_id_.value() != group_id.value())) {
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
