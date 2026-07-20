/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <astl/astl_telemetry.h>

int main(void) {
  astl_target_props_t* targets      = NULL;
  uint32_t             target_count = 0;

  ASTL_INIT_STRUCT(astl_get_target_count_params_t, get_target_count_params, .flags = 0, .target_count = &target_count);
  ASTL_ALLOC_ARRAY(astl_target_props_t, allocated_targets, 1);

  targets = allocated_targets;
  if (targets != NULL) {
    ASTL_FREE_ARRAY(targets);
  }

  return (get_target_count_params.size == sizeof(astl_get_target_count_params_t)) ? 0 : 1;
}
