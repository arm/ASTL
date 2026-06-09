// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "test_utilities.hpp"

#include <google/protobuf/message_lite.h>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

[[noreturn]] auto ExitForkedTestChild(int exit_code) noexcept -> void {
  google::protobuf::ShutdownProtobufLibrary();
  _exit(exit_code);
}
