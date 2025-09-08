#ifndef MOCK_LIBSENSORS_HPP_
#define MOCK_LIBSENSORS_HPP_

#include <sensors/sensors.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp> /* this should go last */

class MockLibsensors {
 public:
  MAKE_MOCK1(sensors_init, int(FILE *));
  MAKE_MOCK0(sensors_cleanup, void());
  MAKE_MOCK1(sensors_free_chip_name, void(sensors_chip_name *chip));
  MAKE_MOCK2(sensors_get_detected_chips, const sensors_chip_name *(const sensors_chip_name *, int *));
  MAKE_MOCK2(sensors_get_features, const sensors_feature *(const sensors_chip_name *, int *));
  MAKE_MOCK3(sensors_get_subfeature,
             const sensors_subfeature *(const sensors_chip_name *, const sensors_feature *, int));
  MAKE_MOCK2(sensors_get_label, char *(const sensors_chip_name *, const sensors_feature *));
  MAKE_MOCK3(sensors_get_value, int(const sensors_chip_name *, int, double *));
  MAKE_MOCK3(sensors_snprintf_chip_name, int(char *, size_t, const sensors_chip_name *));
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)   -- needed for this libsensors test shim
extern MockLibsensors mock_libsensors;

#endif  // MOCK_LIBSENSORS_HPP_
