# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

find_path(ASTL_INSTALL_INCLUDE_DIR astl/astl.h)

find_library(ASTL_INSTALL_LIB_DIR astl)

mark_as_advanced(ASTL_INSTALL_INCLUDE_DIR ASTL_INSTALL_LIB_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(astl REQUIRED_VARS
  ASTL_INSTALL_INCLUDE_DIR
  ASTL_INSTALL_LIB_DIR
  )

if(astl_FOUND AND NOT TARGET Astl::astl)
  add_library(Astl::astl SHARED IMPORTED)
  set_target_properties(Astl::astl PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${ASTL_INSTALL_LIB_DIR}"
    INTERFACE_INCLUDE_DIRECTORIES "${ASTL_INSTALL_INCLUDE_DIR}"
    )
endif()
