// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <span>
#include <string_view>

#include "astl/astl_telemetry.h"

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    return 2;
  }
  if (argc == 3) {
    std::filesystem::current_path(std::span(argv, static_cast<std::size_t>(argc))[2]);
  }
  uint32_t                             count = 0;
  const astl_get_target_count_params_t params{sizeof(params), 0, &count};
  const auto expected = std::string_view(std::span(argv, static_cast<std::size_t>(argc))[1]) == "bad"
                            ? ASTL_STATUS_BAD_CONFIGURATION
                            : ASTL_STATUS_SUCCESS;
  return astlGetTargetCount(&params) == expected ? 0 : 1;
}
