// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MOCK_LIBSENSORS_HPP_
#define MOCK_LIBSENSORS_HPP_

#include <sensors/sensors.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp> /* this should go last */

#include "libsensors/libsensors_api.hpp"

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
inline MockLibsensors *g_sensors_mock = nullptr;  // a pointer to the current mock (managed by the test)

inline int mock_sensors_init(FILE *input) { return g_sensors_mock->sensors_init(input); }

inline void mock_sensors_cleanup() { return g_sensors_mock->sensors_cleanup(); }

inline void mock_sensors_free_chip_name(sensors_chip_name *chip) {
  return g_sensors_mock->sensors_free_chip_name(chip);
}

inline const sensors_chip_name *mock_sensors_get_detected_chips(const sensors_chip_name *match, int *nr) {
  return g_sensors_mock->sensors_get_detected_chips(match, nr);
}

inline const sensors_feature *mock_sensors_get_features(const sensors_chip_name *name, int *nr) {
  return g_sensors_mock->sensors_get_features(name, nr);
}

inline const sensors_subfeature *mock_sensors_get_subfeature(const sensors_chip_name *name,
                                                             const sensors_feature   *feature,
                                                             sensors_subfeature_type  type) {
  return g_sensors_mock->sensors_get_subfeature(name, feature, type);
}
inline char *mock_sensors_get_label(const sensors_chip_name *name, const sensors_feature *feature) {
  return g_sensors_mock->sensors_get_label(name, feature);
}

inline int mock_sensors_get_value(const sensors_chip_name *name, int subfeat_nr, double *value) {
  return g_sensors_mock->sensors_get_value(name, subfeat_nr, value);
}

inline int mock_sensors_snprintf_chip_name(char *str, size_t size, const sensors_chip_name *chip) {
  return g_sensors_mock->sensors_snprintf_chip_name(str, size, chip);
}

// A derived class to allow mocking protected members
class SensorsApiInstantiator : public SensorsApi {
 public:
  explicit SensorsApiInstantiator() : SensorsApi(nullptr) {}
};

// helper to assemble a mocked SensorsApi
inline std::shared_ptr<SensorsApiInstantiator> MakeMockedSensorsApi() {
  auto api                = std::make_shared<SensorsApiInstantiator>();
  api->init               = &mock_sensors_init;
  api->cleanup            = &mock_sensors_cleanup;
  api->get_detected_chips = &mock_sensors_get_detected_chips;
  api->get_features       = &mock_sensors_get_features;
  api->get_subfeature     = &mock_sensors_get_subfeature;
  api->get_label          = &mock_sensors_get_label;
  api->get_value          = &mock_sensors_get_value;
  api->snprintf_chip_name = &mock_sensors_snprintf_chip_name;
  return api;
}

struct MockSensorsApiTestHarness {
 public:
  MockSensorsApiTestHarness() : mock_libsensors{new MockLibsensors()}, api{MakeMockedSensorsApi()} {
    g_sensors_mock = mock_libsensors.get();
  }

  MockSensorsApiTestHarness(const MockSensorsApiTestHarness &)            = delete;
  MockSensorsApiTestHarness &operator=(const MockSensorsApiTestHarness &) = delete;
  MockSensorsApiTestHarness(MockSensorsApiTestHarness &&)                 = delete;
  MockSensorsApiTestHarness &operator=(MockSensorsApiTestHarness &&)      = delete;

  ~MockSensorsApiTestHarness() { g_sensors_mock = nullptr; }

  // the backing mock object where tests can put expectation and behaviors
  std::unique_ptr<MockLibsensors> mock_libsensors;
  // the api wrapper that components under test will use as a substitute for libsensors
  std::shared_ptr<SensorsApi> api;
};

#endif  // MOCK_LIBSENSORS_HPP_
