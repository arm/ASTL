// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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
