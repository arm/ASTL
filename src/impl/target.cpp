// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "target.hpp"

#include <string>

#include "astl/astl.h"
#include "common/string_pool.hpp"

namespace astl {

Target::Target(std::string name, std::string description, CollectorType collector_type, Target* parent,
               std::optional<std::string> uuid)
    : _name{std::move(name)},
      _description{std::move(description)},
      _collector_type{collector_type},
      _parent{parent},
      _uuid{std::move(uuid)} {}

auto Target::GetProperties(astl_target_properties_t* target) const -> astl_status_code {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  target->_handle      = this;
  target->_name        = GetInternedString(_name);
  target->_description = GetInternedString(_description);
  target->_uuid        = _uuid.has_value() ? GetInternedString(*_uuid) : nullptr;
  return ASTL_STATUS_SUCCESS;
}

auto Target::Name() const -> std::string const& { return _name; }
auto Target::GetCollectorType() const -> CollectorType { return _collector_type; }
auto Target::GetParent() const -> const Target* { return _parent; }

}  // namespace astl
