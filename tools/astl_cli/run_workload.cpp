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

#include "run_workload.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#else
#  include <signal.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif
#include <csignal>

// Shared stop flag
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<bool> g_stop{false};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static void (*g_prev_sigint)(int sig) = SIG_DFL;

static constexpr int kWaitIntervalMs = 100;

extern "C" void OnSigInt(int sig) {
  (void)sig;
  g_stop.store(true, std::memory_order_relaxed);
}

#ifdef _WIN32
/**
 * @brief Launches a process + args given by 'command', with optional timeout and Ctrl-C cancellability.
 * Redirects Ctrl-C handler to kill that process. Terminates process after timeout if given.
 *
 * @return int exit code of the process launched (or  -1 on  error,  -2 on timeout);
 */
int RunWorkload(std::string const& command, std::optional<std::chrono::seconds> timeout) {
  g_prev_sigint = std::signal(SIGINT, OnSigInt);
  STARTUPINFOW        startup_info{};
  PROCESS_INFORMATION process_info{};
  startup_info.cb = sizeof(startup_info);

  // Convert UTF-8 to wide
  int          len = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
  std::wstring wcmd(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, &wcmd[0], len);

  if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr,
                      &startup_info, &process_info)) {
    std::cerr << "CreateProcess failed\n";
    return false;
  }

  auto deadline = timeout.has_value()
                      ? std::chrono::steady_clock::time_point(std::chrono::steady_clock::now() + *timeout)
                      : std::chrono::steady_clock::time_point::max();

  bool killed = false;
  while (true) {
    DWORD res = WaitForSingleObject(process_info.hProcess, kWaitIntervalMs);
    if (res == WAIT_OBJECT_0) break;  // process exited
    if (g_stop.load()) {
      std::cerr << "Ctrl-C pressed — terminating child...\n";
      TerminateProcess(process_info.hProcess, 1);
      killed = true;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      std::cerr << "Timeout — terminating child...\n";
      TerminateProcess(process_info.hProcess, 1);
      killed = true;
      break;
    }
  }

  DWORD code = 0;
  GetExitCodeProcess(process_info.hProcess, &code);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  std::cout << "Child exited with code " << code << (killed ? " (killed)" : "") << "\n";
  std::signal(SIGINT, g_prev_sigint);
  return true;
}

#else
/**
 * @brief Launches a process + args given by 'command', with optional timeout and Ctrl-C cancellability.
 * Redirects Ctrl-C handler to kill that process. Terminates process after timeout if given.
 *
 * @return int exit code of the process launched (or -1 on  error, -2 on timeout);
 */
int RunWorkload(std::string const& command, std::optional<std::chrono::seconds> timeout) {
  g_prev_sigint = std::signal(SIGINT, OnSigInt);
  pid_t pid     = fork();
  if (pid < 0) {
    perror("fork");
    return -1;
  }
  if (pid == 0) {
    // child
    // NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast) execvp needs 'char *const *'
    std::vector<char*> process_argv{const_cast<char*>("sh"), const_cast<char*>("-c"),
                                    const_cast<char*>(command.c_str()), nullptr};
    // NOLINTEND(cppcoreguidelines-pro-type-const-cast)
    execvp("sh", process_argv.data());
    perror("exec");
    _exit(-1);
  }

  auto deadline = timeout.has_value()
                      ? std::chrono::steady_clock::time_point(std::chrono::steady_clock::now() + *timeout)
                      : std::chrono::steady_clock::time_point::max();

  bool killed = false;
  while (true) {
    int status = 0;
    if (pid == waitpid(pid, &status, WNOHANG)) {
      break;  // child finished
    }

    if (g_stop.load()) {
      std::cerr << "Ctrl-C pressed — terminating child...\n";
      kill(pid, SIGTERM);
      killed = true;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      std::cerr << "Timeout — terminating child...\n";
      kill(pid, SIGTERM);
      killed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitIntervalMs));
  }

  int status = 0;
  waitpid(pid, &status, 0);
  std::cout << "Child exited " << (WIFEXITED(status) ? "normally" : "abnormally") << (killed ? " (killed)" : "")
            << "\n";
  // Restore the previous SIGINT handler
  std::signal(SIGINT, g_prev_sigint);
  return status;
}
#endif
