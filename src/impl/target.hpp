// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_API_TARGET_H_
#define ASTL_API_TARGET_H_

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "astl/astl.h"
#include "common/capabilities.hpp"

namespace astl {

/**
 * @brief The C++ interface representing an astl_target_handle_t
 */
struct ITarget {
  virtual ~ITarget()                 = default;
  ITarget()                          = default;
  ITarget(const ITarget&)            = default;
  ITarget& operator=(const ITarget&) = default;
  ITarget(ITarget&&)                 = default;
  ITarget& operator=(ITarget&&)      = default;

  virtual auto GetProperties(astl_target_props_t* target) const -> astl_status_code = 0;
  virtual auto Name() const -> std::string const&                                   = 0;
  virtual auto CollectorTargetPath() const -> std::optional<std::string_view> { return std::nullopt; }

  virtual auto GetCollectorType() const -> CollectorType = 0;
};

inline auto CollectorTypeStableKeyPrefix(CollectorType collector_type) -> std::string_view {
  switch (collector_type) {
    case CollectorType::SCMI:
      return "scmi";
    case CollectorType::LIBSENSORS:
      return "libsensors";
    case CollectorType::PROCFS:
      return "procfs";
    case CollectorType::UNKNOWN:
      return "unknown";
  }
  return "unknown";
}

inline auto GetStableTargetKey(const ITarget& target) -> std::string {
  const auto raw_key = [&target]() -> std::string_view {
    if (const auto collector_target_path = target.CollectorTargetPath();
        collector_target_path.has_value() && !collector_target_path->empty()) {
      return *collector_target_path;
    }
    return target.Name();
  }();

  std::string stable_key =
      std::string{CollectorTypeStableKeyPrefix(target.GetCollectorType())} + "__" + std::string{raw_key};
  std::replace_if(
      stable_key.begin(), stable_key.end(),
      [](char character) { return character == '/' || character == '\\' || character == ':'; }, '_');
  return stable_key;
}

/**
 * @brief A partial implementation of the ITarget interface that holds data to fill a astl_target_props_t struct
 */
class Target : public astl::ITarget {
 public:
  Target() = default;
  Target(std::string name, std::string description, CollectorType collector_type, Target* parent = nullptr,
         std::optional<std::string> uuid                  = std::nullopt,
         std::optional<std::string> collector_target_path = std::nullopt);
  ~Target() override               = default;
  Target(const Target&)            = default;
  Target& operator=(const Target&) = default;
  Target(Target&&)                 = default;
  Target& operator=(Target&&)      = default;

  auto GetProperties(astl_target_props_t* target) const -> astl_status_code override;
  auto Name() const -> std::string const& override;
  auto CollectorTargetPath() const -> std::optional<std::string_view> override;
  auto GetCollectorType() const -> CollectorType override;
  auto GetParent() const -> const Target*;
  auto GetUuid() const -> const std::optional<std::string>& { return _uuid; }
  auto SetName(std::string name) -> void;
  auto SetCollectorTargetPath(std::optional<std::string> collector_target_path) -> void;

 private:
  std::string                _name;
  std::string                _description;
  CollectorType              _collector_type{CollectorType::UNKNOWN};
  Target*                    _parent{nullptr};
  std::optional<std::string> _uuid;
  std::optional<std::string> _collector_target_path;
};

}  // namespace astl

#endif  // ASTL_API_TARGET_H_
