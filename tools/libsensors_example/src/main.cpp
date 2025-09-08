
// based on https://linux.die.net/man/3/libsensors
// requires package 'libsensors-dev'

#include <sensors/sensors.h>

#include <array>
#include <iostream>

void HandleTempSensor(const sensors_chip_name* chip, const sensors_feature* feature) {
  const sensors_subfeature* sub = sensors_get_subfeature(chip, feature, SENSORS_SUBFEATURE_TEMP_INPUT);
  if (sub && sub->flags & SENSORS_MODE_R) {
    double temp   = 0.0;
    auto   result = sensors_get_value(chip, sub->number, &temp);
    if (result == 0) {
      std::cout << "Sensor: " << sensors_get_label(chip, feature) << " = " << temp << "°C\n";
    } else {
      std::cerr << "Failed (" << result << ") to read temperature for sensor: " << sensors_get_label(chip, feature)
                << "\n";
    }
  }
}

void HandlePowerSensor(const sensors_chip_name* chip, const sensors_feature* feature) {
  const sensors_subfeature* sub = sensors_get_subfeature(chip, feature, SENSORS_SUBFEATURE_POWER_INPUT);
  if (sub && sub->flags & SENSORS_MODE_R) {
    double power  = 0.0;
    auto   result = sensors_get_value(chip, sub->number, &power);
    if (result == 0) {
      std::cout << "Sensor: " << sensors_get_label(chip, feature) << " = " << power << " W\n";
    } else {
      std::cerr << "Failed (" << result << ") to read power for sensor: " << sensors_get_label(chip, feature) << "\n";
    }
  }
}

int main() {
  // Initialize sensors
  if (sensors_init(nullptr) != 0) {
    std::cerr << "Failed to initialize libsensors.\n";
    return 1;
  }

  const sensors_chip_name* chip       = nullptr;
  int                      chip_index = 0;
  while ((chip = sensors_get_detected_chips(nullptr, &chip_index))) {
    const sensors_feature*            feature              = nullptr;
    int                               sensor_feature_count = 0;
    constexpr size_t                  max_name_length      = 200;
    std::array<char, max_name_length> chip_name{'\0'};
    sensors_snprintf_chip_name(chip_name.data(), max_name_length, chip);
    std::cout << "\nScanning " << chip_name.data() << " for features\n";
    while ((feature = sensors_get_features(chip, &sensor_feature_count))) {
      switch (feature->type) {
        case SENSORS_FEATURE_TEMP:
          HandleTempSensor(chip, feature);
          break;
        case SENSORS_FEATURE_POWER:
          HandlePowerSensor(chip, feature);
          break;
        default:
          std::cout << "Skipping unrecognized feature: " << sensors_get_label(chip, feature) << "\n";
      }
    }
  }
  sensors_cleanup();
  return 0;
}
