// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef NUMA_OPERATION_BUILDER_HPP_
#define NUMA_OPERATION_BUILDER_HPP_
#include "operation/operation.hpp"
#include "target.hpp"
namespace astl {
enum class NumaField { HIT, MISS, INTERLEAVE_HIT, LOCAL_NODE, OTHER_NODE, LOCAL_PERCENT, REMOTE_PERCENT, ALLOCATIONS };
struct NumaReadOperation : Operation {
  NumaField   field;
  std::string node;
  NumaReadOperation(NumaField selected, std::string instance) : field(selected), node(std::move(instance)) {}
};
class NumaOperationBuilder {
 public:
  explicit NumaOperationBuilder(NumaField field, std::string node = {}) : _field(field), _node(std::move(node)) {}
  auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code> {
    if (!target || target->GetCollectorType() != CollectorType::NUMA) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    OperationSequence result;
    result.push_back(std::make_unique<NumaReadOperation>(_field, _node));
    return result;
  }

 private:
  NumaField   _field;
  std::string _node;
};
}  // namespace astl
#endif
