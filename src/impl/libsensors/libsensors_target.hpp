// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef LIBSENSORS_TARGET_HPP_
#define LIBSENSORS_TARGET_HPP_

#include <memory>
#include <string>

#include "astl/astl.h"
#include "common/capabilities.hpp"
#include "target.hpp"
#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "libsensors/libsensors_api.hpp"
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)

namespace astl {

#if defined(ASTL_INCLUDE_LIBSENSORS)
/**
 * @brief A libsensors-specific implementation of the astl::Target interface
 */
class LibsensorsTarget : public astl::Target {
 public:
  LibsensorsTarget(std::string name, std::string description, std::string chip_name,
                   std::shared_ptr<SensorsApi> libsensors_api)
      : astl::Target(std::move(name), std::move(description), CollectorType::LIBSENSORS),
        _chip_name{std::move(chip_name)},
        _libsensors_api{std::move(libsensors_api)} {}

  ~LibsensorsTarget() override                         = default;
  LibsensorsTarget(const LibsensorsTarget&)            = default;
  LibsensorsTarget& operator=(const LibsensorsTarget&) = default;
  LibsensorsTarget(LibsensorsTarget&&)                 = default;
  LibsensorsTarget& operator=(LibsensorsTarget&&)      = default;

  auto ChipName() const -> std::string const& { return _chip_name; }
  auto ShareApi() const -> std::shared_ptr<SensorsApi> { return _libsensors_api; }

 private:
  std::string                 _chip_name;
  std::shared_ptr<SensorsApi> _libsensors_api;
};
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)

}  // namespace astl

#endif  // LIBSENSORS_TARGET_HPP_
