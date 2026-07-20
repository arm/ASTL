// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file libsensors.hpp
 * @brief Operation specialization for reading sensor values via libsensors.
 */
#ifndef SENSORS_HPP_
#define SENSORS_HPP_

#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include <sensors/sensors.h>
#endif

#include "operation/operation.hpp"

namespace astl {

#if defined(ASTL_INCLUDE_LIBSENSORS)
/**
 * @brief Operation describing a single libsensors read of a subfeature on a chip.
 *
 * Validates construction arguments to avoid null chip pointers at runtime.
 */
struct LibsensorsReadOperation : public Operation {
  const sensors_chip_name* chip;
  int                      subfeature_number{0};

  LibsensorsReadOperation() = delete;
  LibsensorsReadOperation(const sensors_chip_name* chip, int subfeature_number)
      : chip{chip}, subfeature_number{subfeature_number} {
    if (!chip) {
      throw std::invalid_argument("LibsensorsReadOperation requires non-null chip");
    }
  }
};
#endif

}  // namespace astl

#endif  // SENSORS_HPP_
