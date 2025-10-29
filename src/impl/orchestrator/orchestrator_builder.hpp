/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2025 Arm Limited and/or its affiliates
 * SPDX-FileCopyrightText: <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 ******************************************************************************/

#ifndef ORCHESTRATOR_BUILDER_HPP_
#define ORCHESTRATOR_BUILDER_HPP_

#include "astl/astl_errors.h"
#include "astl/astl_telemetry.h"
#include "config/astl_configuration.hpp"

[[nodiscard]] auto BuildOrchestrator(const astl::AstlConfiguration& configuration) -> astl_status_code;

#endif  // ORCHESTRATOR_BUILDER_HPP_
