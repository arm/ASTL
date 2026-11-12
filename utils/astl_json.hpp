// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_JSON_HPP_
#define ASTL_JSON_HPP_

#include <nlohmann/json.hpp>

#include "astl_cxx_compat.hpp"

// nlohmann_json supports std::optional directly. Register this adapter only when the alias selects the fallback,
// preserving the native serializer and the same null/value representation in both modes.
#if !defined(__cpp_lib_optional) || __cpp_lib_optional < 202110L
NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename T>
struct adl_serializer<astl::compat::optional<T>> {
  template <typename Json>
  static auto to_json(Json& json, const astl::compat::optional<T>& value) -> void {
    if (value) {
      json = *value;
    } else {
      json = nullptr;
    }
  }

  template <typename Json>
  static auto from_json(const Json& json, astl::compat::optional<T>& value) -> void {
    if (json.is_null()) {
      value = astl::compat::nullopt;
    } else {
      value = json.template get<T>();
    }
  }
};
NLOHMANN_JSON_NAMESPACE_END
#endif

#endif  // ASTL_JSON_HPP_
