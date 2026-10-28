// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef OPERATION_BUILDER_HPP_
#define OPERATION_BUILDER_HPP_

#include <concepts>
#include <functional>
#include <utility>

#include "astl_internal_status.hpp"
#include "operation/operation.hpp"  // defines OperationSequence and OperationBuilder concept
#include "target.hpp"

namespace astl {

/**
 * @brief A no-op operation builder that always returns NOT_IMPLEMENTED
 *
 * Useful as a default placeholder in AnyOperationBuilder
 */
class NullOperationBuilder {
 public:
  NullOperationBuilder() = default;

  [[nodiscard]] static auto BuildOperations(const ITarget* target)
      -> std::expected<OperationSequence, astl_status_code> {
    (void)target;
    return std::unexpected(astl::kInternalNotImplemented);
  }
};

static_assert(OperationBuilder<NullOperationBuilder>, "NullOperationBuilder does not satisfy OperationBuilder concept");

// An owning, copyable strategy. std::function handles storage without a fixed size limit.
// Concrete builders remain independent of this wrapper and are invoked through const references.
class AnyOperationBuilder {
  template <OperationBuilder T>
  struct Adapter {
    T    builder;
    auto operator()(const ITarget* target) const { return builder.BuildOperations(target); }
  };

 public:
  AnyOperationBuilder() : AnyOperationBuilder(NullOperationBuilder{}) {}

  template <OperationBuilder T>
  requires(!std::same_as<std::remove_cvref_t<T>, AnyOperationBuilder>) &&
          std::copy_constructible<std::remove_cvref_t<T>>
  // NOLINTNEXTLINE(google-explicit-constructor) -- Preserve variant-style conversion in aggregate metric configs.
  AnyOperationBuilder(T builder) : _build(Adapter<T>{std::move(builder)}) {}

  template <OperationBuilder T>
  auto Target() const -> const T* {
    const auto* adapter = _build.template target<Adapter<T>>();
    return adapter ? &adapter->builder : nullptr;
  }

  auto BuildOperations(const ITarget* target) const -> std::expected<OperationSequence, astl_status_code> {
    return _build ? _build(target) : NullOperationBuilder::BuildOperations(target);
  }

 private:
  std::function<std::expected<OperationSequence, astl_status_code>(const ITarget*)> _build;
};

template <typename OperationBuilderType>
concept AnyOperationBuilderCompatible = std::constructible_from<AnyOperationBuilder, OperationBuilderType&&>;

/**
 * @brief Use the given builder to create operations for the given target
 *
 * @param builder The operation builder to use, satisfies the OperationBuilder concept
 *                can be stored in AnyOperationBuilder
 * @param target The target for which to build operations,
 *               should match the collector type (.eg SCMI, Libsensors) of the builder
 *
 * @return either a sequence of Operations or an error code
 */
inline auto BuildOperations(const AnyOperationBuilder& builder, const ITarget* target)
    -> std::expected<OperationSequence, astl_status_code> {
  try {
    return builder.BuildOperations(target);
  } catch (const OperationIdExhausted& ex) {
    ASTL_LOG_ERROR("BuildOperations: {}", ex.what());
    return std::unexpected{OperationIdExhausted::Status()};
  }
}

}  // namespace astl

#endif  // OPERATION_BUILDER_HPP_
