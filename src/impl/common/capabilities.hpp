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

#ifndef CAPABILITIES_HPP_
#define CAPABILITIES_HPP_

#include <vector>

namespace astl {

enum class CollectorType { SCMI, MMIO };

/**
 * @brief Attributes of a collector indicating what it's able to do.
 * Alternatively, requirements of an operation or metric
 */
struct CollectorCapability {
  CollectorType collector_type;

  CollectorCapability() = delete;
  explicit CollectorCapability(CollectorType collector_type) : collector_type{collector_type} {}

  /// @brief Returns the collector type.
  CollectorType GetCollectorType() const { return collector_type; }
};

struct SystemCapability {
  std::vector<CollectorCapability> collector_capabilities;

  SystemCapability() = default;
  explicit SystemCapability(std::vector<CollectorCapability> &&collector_capabilities)
      : collector_capabilities{std::move(collector_capabilities)} {}
};

/**
 * @brief Encapsulates both individual collector capabilities and system-wide capabilities.
 */
struct Capabilities {
  std::vector<CollectorCapability> _collector_capabilities;
  std::vector<SystemCapability>    _system_capabilities;

  Capabilities(std::vector<CollectorCapability> &&collector_capabilities,
               std::vector<SystemCapability>    &&system_capabilities)
      : _collector_capabilities{std::move(collector_capabilities)},
        _system_capabilities{std::move(system_capabilities)} {}

  /// @brief Returns the collector capabilities.
  const std::vector<CollectorCapability> &GetCollectorCapability() const { return _collector_capabilities; }

  /// @brief Returns the system capabilities.
  const std::vector<SystemCapability> &GetSystemCapabilities() const { return _system_capabilities; }
};

}  // namespace astl

#endif  // CAPABILITIES_HPP_