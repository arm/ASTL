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

#ifndef LIBSENSORS_API_HPP
#define LIBSENSORS_API_HPP

#include <sensors/sensors.h>

#include <memory>
#include <string>

/**
 * @brief Wrapper around dynamically loaded libsensors functions
 *        Accessible via shared_ptr to allow sharing between multiple users like Collector and Topology plugins
 */
struct SensorsApi : public std::enable_shared_from_this<SensorsApi> {
 public:
  static auto Create(std::string explicit_path = {}) -> std::shared_ptr<SensorsApi>;

  // Non-copyable, movable
  SensorsApi(const SensorsApi&)            = delete;
  SensorsApi& operator=(const SensorsApi&) = delete;
  SensorsApi(SensorsApi&& other) noexcept;
  SensorsApi& operator=(SensorsApi&& other) noexcept;
  ~SensorsApi() noexcept;

  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  // Function pointers from the lm-sensors library
  decltype(&::sensors_init)               init{nullptr};
  decltype(&::sensors_cleanup)            cleanup{nullptr};
  decltype(&::sensors_snprintf_chip_name) snprintf_chip_name{nullptr};
  decltype(&::sensors_get_label)          get_label{nullptr};
  decltype(&::sensors_get_value)          get_value{nullptr};
  decltype(&::sensors_get_detected_chips) get_detected_chips{nullptr};
  decltype(&::sensors_get_features)       get_features{nullptr};
  decltype(&::sensors_get_subfeature)     get_subfeature{nullptr};
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

  auto Ok() const noexcept -> bool;

 protected:
  // Private ctor - use Create()
  explicit SensorsApi(void* handle);

 private:
  void* _handle{nullptr};
};

#endif  // LIBSENSORS_API_HPP