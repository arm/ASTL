// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef ASTL_MAGIC_ENUM_HPP_
#define ASTL_MAGIC_ENUM_HPP_

#if __has_include(<magic_enum/magic_enum.hpp>)
#  include <magic_enum/magic_enum.hpp>
#elif __has_include(<magic_enum.hpp>)
#  include <magic_enum.hpp>
#else
#  include <magic_enum/magic_enum.hpp>
#endif

#endif  // ASTL_MAGIC_ENUM_HPP_
