#include "target.hpp"

#include <string>
#include <vector>

#include "astl/astl.h"

namespace astl {

Target::Target(std::string name, std::string description, Target* parent)
    : _name{std::move(name)}, _description{std::move(description)}, _parent{parent} {}

astl_status_code Target::GetProperties(astl_target_properties_t* target) {
  if (!target) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  target->_handle      = this;
  target->_name        = _name.c_str();
  target->_description = _description.c_str();
  return ASTL_STATUS_SUCCESS;
}

}  // namespace astl