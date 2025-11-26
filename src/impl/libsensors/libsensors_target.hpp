/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

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
  LibsensorsTarget(std::string name, std::string description, std::shared_ptr<SensorsApi> libsensors_api)
      : astl::Target(std::move(name), std::move(description), CollectorType::LIBSENSORS),
        _libsensors_api{std::move(libsensors_api)} {}

  ~LibsensorsTarget() override                         = default;
  LibsensorsTarget(const LibsensorsTarget&)            = default;
  LibsensorsTarget& operator=(const LibsensorsTarget&) = default;
  LibsensorsTarget(LibsensorsTarget&&)                 = default;
  LibsensorsTarget& operator=(LibsensorsTarget&&)      = default;

  auto ShareApi() const -> std::shared_ptr<SensorsApi> { return _libsensors_api; }

 private:
  std::shared_ptr<SensorsApi> _libsensors_api;
};
#endif  // defined(ASTL_INCLUDE_LIBSENSORS)

}  // namespace astl

#endif  // LIBSENSORS_TARGET_HPP_
