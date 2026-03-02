// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ORCHESTRATOR_BUILDER_HPP_
#define ORCHESTRATOR_BUILDER_HPP_

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "config/astl_configuration.hpp"

[[nodiscard]] auto BuildOrchestrator(const astl::AstlConfiguration& configuration) -> astl_status_code;

#endif  // ORCHESTRATOR_BUILDER_HPP_
