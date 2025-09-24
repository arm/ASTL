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

#ifndef ASTL_DEFINES_HPP_
#define ASTL_DEFINES_HPP_

#include <unordered_map>

#include "metric/i_metric.hpp"
#include "target.hpp"

namespace astl {

using RawSamplesMap = std::unordered_map<const ITarget*, std::vector<RawSampledData>>;

using ProcessedSamplesMap =
    std::unordered_map<const ITarget*, std::unordered_map<const IMetric*, std::vector<ProcessedSampledData>>>;

}  // namespace astl
#endif  // ASTL_DEFINES_HPP_