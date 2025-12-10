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

#ifndef COLLECTION_OPERATIONS_HPP_
#define COLLECTION_OPERATIONS_HPP_

#include "common/capabilities.hpp"
#include "operation/operation.hpp"

namespace astl {

// Everything a collector needs to know to start, stop, pause, resume a set of counters or metrics,
// as well as how often to sample. Metric manager should provide this set of operations,
// Collector manager should decide which collector executes them, and concrete collectors will cast these
// operations to concrete types to actually run them.
struct CollectionOperations {
  OperationSequence   operationsBeforeStart;
  OperationSequence   operationsAtStart;
  OperationSequence   operationsOnSample;
  OperationSequence   operationsAtStop;
  SamplingInterval    samplingInterval;
  CollectorCapability requirements;
};

}  // namespace astl

#endif  // COLLECTION_OPERATIONS_HPP_
