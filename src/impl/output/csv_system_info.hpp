/*******************************************************************************
 * SPDX-FileCopyrightText: Copyright (C) 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

#ifndef CSV_SYSTEM_INFO_HPP_
#define CSV_SYSTEM_INFO_HPP_

#include <iosfwd>

namespace astl {

void WriteSystemInfoCsvSection(std::ostream& output_stream);

}  // namespace astl

#endif  // CSV_SYSTEM_INFO_HPP_
