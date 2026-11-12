// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_EMPTY_RESULT_REASON_HPP_
#define ASTL_EMPTY_RESULT_REASON_HPP_

namespace astl {
// Latest input outcome, converted to a public status at public retrieval and immediate-read boundaries.
enum class EmptyResultReason { UNKNOWN, NO_NEW_SAMPLE, SAMPLE_DROPPED };
}  // namespace astl

#endif
