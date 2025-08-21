#include "target.hpp"

#include <string>
#include <vector>

#include "astl/astl.h"

namespace astl {

Target::Target(std::string name, std::string description, CollectorType collector_type, Target* parent)
    : _name{std::move(name)}, _description{std::move(description)}, _collector_type{collector_type}, _parent{parent} {}

astl_status_code Target::GetProperties(astl_target_properties_t* target) {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  target->_handle      = this;
  target->_name        = _name.c_str();
  target->_description = _description.c_str();
  return ASTL_STATUS_SUCCESS;
}

std::string const&                            Target::Name() const { return _name; }
CollectorType                                 Target::GetCollectorType() const { return _collector_type; }
const Target*                                 Target::GetParent() const { return _parent; }
size_t                                        Target::GetCounterCount() const { return _counters.size(); }
const std::vector<std::unique_ptr<ICounter>>& Target::GetCounters() const { return _counters; }

}  // namespace astl