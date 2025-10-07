#ifndef ASTL_API_COUNTER_H_
#define ASTL_API_COUNTER_H_

#include <string>

#include "astl/astl.h"
#include "common/capabilities.hpp"

namespace astl {

/**
 * @brief The internal representation of the astl_counter_handle_t entities
 *
 * This defines the functional interface of a Counter. Specializations and Mocks for testing should inherit this
 */
struct ICounter {
  /**
   * @brief allow destroying counter instances by base class pointer
   */
  virtual ~ICounter() = default;

  ICounter()                            = default;
  ICounter(const ICounter &)            = default;
  ICounter &operator=(const ICounter &) = default;
  ICounter(ICounter &&)                 = default;
  ICounter &operator=(ICounter &&)      = default;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  virtual auto GetProperties(astl_counter_properties_t *properties) const -> astl_status_code = 0;

  /**
   * @brief Prepare for collection based on the given set of parameters
   */
  virtual auto ConfigureCollection(astl_collection_parameters_t const *parameters) -> astl_status_code = 0;
};

/**
 * @brief C++ representation of an astl_counter_properties_t
 */
struct CounterProperties {
 public:
  CounterProperties(std::string name, std::string description, uint32_t min_sampling_interval_ms, astl_units_t units,
                    uint64_t mask, std::string formula, astl_value_type_t value_type, astl_counter_type_t counter_type);

  std::string         _name;
  std::string         _description;
  uint32_t            _min_sampling_interval_ms = 0;
  astl_units_t        _units                    = ASTL_UNITS_NONE;
  uint64_t            _mask                     = 0x0;
  std::string         _formula;
  astl_value_type_t   _value_type   = ASTL_VALUE_UNKNOWN;
  astl_counter_type_t _counter_type = ASTL_COUNTER_TYPE_UNKNOWN;
};

class Counter : public ICounter {
 public:
  /**
   * @brief Counters must be created with a CounterProperties instance
   */
  Counter() = delete;

  /**
   * @brief Initialize this counter instance with properties to be used in GetProperties
   */
  explicit Counter(CounterProperties properties) : _properties{std::move(properties)} {};

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  auto GetProperties(astl_counter_properties_t *properties) const -> astl_status_code override;

 private:
  CounterProperties _properties;
};

}  // namespace astl

#endif  // ASTL_API_COUNTER_H_
