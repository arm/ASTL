
#include <counter.hpp>

astl::CounterProperties::CounterProperties(std::string name, std::string description, uint32_t min_sampling_interval_ms,
                                           astl_units_t units, uint64_t mask, std::string formula,
                                           astl_value_type_t value_type, astl_counter_type_t counter_type)
    : _name(std::move(name)),
      _description(std::move(description)),
      _min_sampling_interval_ms(min_sampling_interval_ms),
      _units(units),
      _mask(mask),
      _formula(std::move(formula)),
      _value_type(value_type),
      _counter_type(counter_type) {}

astl_status_code astl::Counter::GetProperties(astl_counter_properties_t *properties) {
  if (!properties) {
    return ASTL_STATUS_BAD_ARGUMENT;
  }
  properties->_handle                = this;
  properties->_name                  = _properties._name.c_str();
  properties->_description           = _properties._description.c_str();
  properties->_min_sampling_interval = _properties._min_sampling_interval_ms;
  properties->_units                 = _properties._units;
  properties->_mask                  = _properties._mask;
  properties->_formula               = _properties._formula.c_str();
  properties->_value_type            = _properties._value_type;
  properties->_counter_type          = _properties._counter_type;
  return ASTL_STATUS_SUCCESS;
}
