#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ipb::ipb-router" for configuration "Release"
set_property(TARGET ipb::ipb-router APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(ipb::ipb-router PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libipb-router.a"
  )

list(APPEND _cmake_import_check_targets ipb::ipb-router )
list(APPEND _cmake_import_check_files_for_ipb::ipb-router "${_IMPORT_PREFIX}/lib/libipb-router.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
