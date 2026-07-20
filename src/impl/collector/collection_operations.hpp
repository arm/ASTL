// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

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
