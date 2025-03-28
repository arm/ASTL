find_path(ATL_INSTALL_INCLUDE_DIR atl/atl.h)

find_library(ATL_INSTALL_LIB_DIR atl)

mark_as_advanced(ATL_INSTALL_INCLUDE_DIR ATL_INSTALL_LIB_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(atl REQUIRED_VARS
  ATL_INSTALL_INCLUDE_DIR
  ATL_INSTALL_LIB_DIR
  )

if(atl_FOUND AND NOT TARGET Atl::atl)
  add_library(Atl::atl SHARED IMPORTED)
  set_target_properties(Atl::atl PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${ATL_INSTALL_LIB_DIR}"
    INTERFACE_INCLUDE_DIRECTORIES "${ATL_INSTALL_INCLUDE_DIR}"
    )
endif()
