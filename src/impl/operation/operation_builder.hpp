// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef OPERATION_BUILDER_HPP_
#define OPERATION_BUILDER_HPP_

#include "operation/operation.hpp"  // defines OperationSequence and OperationBuilder concept
#include "target.hpp"

#if defined(ASTL_INCLUDE_LIBSENSORS)
#  include "libsensors/libsensors_operation_builder.hpp"
#endif
#include "scmi_operation_builder.hpp"

namespace astl {

/**
 * @brief A no-op operation builder that always returns NOT_IMPLEMENTED
 *
 * Useful as a default placeholder in AnyOperationBuilder variant
 */
class NullOperationBuilder {
 public:
  NullOperationBuilder() = default;

  [[nodiscard]] static auto BuildOperations(const ITarget* target)
      -> std::expected<OperationSequence, astl_status_code> {
    (void)target;
    return std::unexpected(ASTL_STATUS_NOT_IMPLEMENTED);
  }
};

static_assert(OperationBuilder<NullOperationBuilder>, "NullOperationBuilder does not satisfy OperationBuilder concept");

using AnyOperationBuilder = std::variant<NullOperationBuilder,
#if defined(ASTL_INCLUDE_LIBSENSORS)
                                         LibsensorsOperationBuilder,
#endif
                                         ScmiOperationBuilder, ScmiMultiTargetOperationBuilder>;

/**
 * @brief Use the given builder to create operations for the given target
 *
 * @param builder The operation builder to use, satisfies the OperationBuilder concept
 *                and is part of the 'AnyOperationBuilder' variant
 * @param target The target for which to build operations,
 *               should match the collector type (.eg SCMI, Libsensors) of the builder
 *
 * @return either a sequence of Operations or an error code
 */
inline auto BuildOperations(const AnyOperationBuilder& builder, const ITarget* target)
    -> std::expected<OperationSequence, astl_status_code> {
  return std::visit(
      [target](const auto& bldr) -> std::expected<OperationSequence, astl_status_code> {
        return bldr.BuildOperations(target);
      },
      builder);
}

}  // namespace astl

#endif  // OPERATION_BUILDER_HPP_
