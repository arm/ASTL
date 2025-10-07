#include "target.hpp"

#include <string>
#include <vector>

#include "astl/astl.h"

namespace astl {

Target::Target(std::string name, std::string description, CollectorType collector_type, Target* parent)
    : _name{std::move(name)}, _description{std::move(description)}, _collector_type{collector_type}, _parent{parent} {}

auto Target::GetProperties(astl_target_properties_t* target) const -> astl_status_code {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  target->_handle      = this;
  target->_name        = _name.c_str();
  target->_description = _description.c_str();
  return ASTL_STATUS_SUCCESS;
}

auto Target::Name() const -> std::string const& { return _name; }
auto Target::GetCollectorType() const -> CollectorType { return _collector_type; }
auto Target::GetParent() const -> const Target* { return _parent; }
auto Target::GetCounterCount() const -> size_t { return _counters.size(); }
auto Target::GetCounters() const -> const std::vector<std::unique_ptr<ICounter>>& { return _counters; }

}  // namespace astl