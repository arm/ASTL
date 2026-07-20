// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "target.hpp"

#include <string>

#include "astl/astl.h"
#include "common/string_pool.hpp"

namespace astl {

Target::Target(std::string name, std::string description, CollectorType collector_type, Target* parent,
               std::optional<std::string> uuid, std::optional<std::string> collector_target_path)
    : _name{std::move(name)},
      _description{std::move(description)},
      _collector_type{collector_type},
      _parent{parent},
      _uuid{std::move(uuid)},
      _collector_target_path{std::move(collector_target_path)} {}

auto Target::GetProperties(astl_target_props_t* target) const -> astl_status_code {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  target->handle      = this;
  target->name        = GetInternedString(_name);
  target->description = GetInternedString(_description);
  target->id          = _uuid.has_value() ? GetInternedString(*_uuid) : nullptr;
  return ASTL_STATUS_SUCCESS;
}

auto Target::Name() const -> std::string const& { return _name; }
auto Target::CollectorTargetPath() const -> std::optional<std::string_view> {
  if (!_collector_target_path.has_value()) {
    return std::nullopt;
  }
  return std::string_view{*_collector_target_path};
}
auto Target::GetCollectorType() const -> CollectorType { return _collector_type; }
auto Target::GetParent() const -> const Target* { return _parent; }
auto Target::SetName(std::string name) -> void { _name = std::move(name); }
auto Target::SetCollectorTargetPath(std::optional<std::string> collector_target_path) -> void {
  _collector_target_path = std::move(collector_target_path);
}

}  // namespace astl
