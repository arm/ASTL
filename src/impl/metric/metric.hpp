#ifndef ASTL_API_METRIC_HPP_
#define ASTL_API_METRIC_HPP_

#include <string>

#include "astl/astl.h"

namespace astl {

/**
 * @brief The internal representation of the astl_metric_handle_t entities
 *
 * This defines the functional interface of a Metric. Specializations and Mocks for testing should inherit this
 */
struct IMetric {
  /**
   * @brief allow destroying metric instances by base class pointer
   */
  virtual ~IMetric() = default;

  IMetric()                           = default;
  IMetric(const IMetric &)            = default;
  IMetric &operator=(const IMetric &) = default;
  IMetric(IMetric &&)                 = default;
  IMetric &operator=(IMetric &&)      = default;

  /**
   * @brief Assign values such as name, units, etc to the given properties pointer.
   */
  virtual astl_status_code GetProperties(astl_metric_properties_t *properties) = 0;
};

}  // namespace astl

#endif  // ASTL_API_METRIC_HPP_
