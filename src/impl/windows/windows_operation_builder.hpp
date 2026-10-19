// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef WINDOWS_OPERATION_BUILDER_HPP_
#define WINDOWS_OPERATION_BUILDER_HPP_
#include <string>

#include "operation/operation.hpp"
#include "target.hpp"

namespace astl {
enum class WindowsSystemField { CPU_UTILIZATION, MEMORY_TOTAL, MEMORY_USED, MEMORY_UTILIZATION };
struct WindowsReadOperation : Operation {
  WindowsSystemField field;
  std::string        instance;
  WindowsReadOperation(WindowsSystemField selected_field, std::string selected_instance)
      : field(selected_field), instance(std::move(selected_instance)) {}
};
class WindowsOperationBuilder {
 public:
  explicit WindowsOperationBuilder(WindowsSystemField field, std::string instance = {})
      : _field(field), _instance(std::move(instance)) {}
  auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code> {
    if (target == nullptr || target->GetCollectorType() != CollectorType::WINDOWS) {
      return std::unexpected(ASTL_STATUS_BAD_ARGUMENT);
    }
    OperationSequence operations;
    operations.push_back(std::make_unique<WindowsReadOperation>(_field, _instance));
    return operations;
  }

 private:
  WindowsSystemField _field;
  std::string        _instance;
};
}  // namespace astl
#endif
