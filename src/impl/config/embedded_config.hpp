// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <span>
#include <string_view>
namespace astl::config {
struct EmbeddedResource {
  std::string_view path;
  std::string_view data;
};
auto EmbeddedResources() -> std::span<const EmbeddedResource>;
}  // namespace astl::config
