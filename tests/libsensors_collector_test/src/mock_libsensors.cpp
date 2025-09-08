#include "mock_libsensors.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp> /* this should go last */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) -- we need non-const to set expectations on it
MockLibsensors mock_libsensors;

extern "C" {

int  sensors_init(FILE *input) { return mock_libsensors.sensors_init(input); }
void sensors_cleanup(void) { mock_libsensors.sensors_cleanup(); }
/*
int sensors_parse_chip_name(const char *orig_name, sensors_chip_name *res) {
    return mock_libsensors.sensors_parse_chip_name(orig_name, res);
}
*/
void sensors_free_chip_name(sensors_chip_name *chip) { mock_libsensors.sensors_free_chip_name(chip); }

int sensors_snprintf_chip_name(char *str, size_t size, const sensors_chip_name *chip) {
  return mock_libsensors.sensors_snprintf_chip_name(str, size, chip);
}
/*
const char *sensors_get_adapter_name(const sensors_bus_id *bus) {
    return mock_libsensors.sensors_get_adapter_name(bus);
}
*/

// NOLINTNEXTLINE(readability-identifier-length) -- should match names from the original C header
const sensors_chip_name *sensors_get_detected_chips(const sensors_chip_name *match, int *nr) {
  return mock_libsensors.sensors_get_detected_chips(match, nr);
}
// NOLINTNEXTLINE(readability-identifier-length) -- should match names from the original C header
const sensors_feature *sensors_get_features(const sensors_chip_name *name, int *nr) {
  return mock_libsensors.sensors_get_features(name, nr);
}
/*
const sensors_subfeature *sensors_get_all_subfeatures(const sensors_chip_name *name, const sensors_feature *feature, int
*nr) { return mock_libsensors.sensors_get_all_subfeatures(name, feature, nr);
}
*/
const sensors_subfeature *sensors_get_subfeature(const sensors_chip_name *name, const sensors_feature *feature,
                                                 sensors_subfeature_type type) {
  return mock_libsensors.sensors_get_subfeature(name, feature, type);
}

char *sensors_get_label(const sensors_chip_name *name, const sensors_feature *feature) {
  return mock_libsensors.sensors_get_label(name, feature);
}

int sensors_get_value(const sensors_chip_name *name, int subfeat_nr, double *value) {
  return mock_libsensors.sensors_get_value(name, subfeat_nr, value);
}
/*
int sensors_set_value(const sensors_chip_name *name, int subfeat_nr, double value) {
    return mock_libsensors.sensors_set_value(name, subfeat_nr, value);
}
int sensors_do_chip_sets(const sensors_chip_name *name) {
    return mock_libsensors.sensors_do_chip_sets(name);
}
const char *sensors_strerror(int errnum) {
    return mock_libsensors.sensors_strerror(errnum);
}
*/
}
