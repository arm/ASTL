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

#ifndef LAUNCH_WORKLOAD_HPP
#define LAUNCH_WORKLOAD_HPP

#include <chrono>
#include <optional>
#include <string>

/**
 * @brief Launches a process + args given by 'command', with optional timeout and Ctrl-C cancellability.
 * Redirects Ctrl-C handler to kill that process. Terminates process after timeout if given.
 *
 * @return int exit code of the process launched (or  -1 on  error,  -2 on timeout);
 */
int RunWorkload(std::string const& command, std::optional<std::chrono::seconds> timeout = std::nullopt);

#endif  // LAUNCH_WORKLOAD_HPP