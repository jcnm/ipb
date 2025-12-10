#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ipb::ipb-sink-console" for configuration "Release"
set_property(TARGET ipb::ipb-sink-console APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(ipb::ipb-sink-console PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libipb-sink-console.1.0.0.dylib"
  IMPORTED_SONAME_RELEASE "@rpath/libipb-sink-console.1.dylib"
  )

list(APPEND _cmake_import_check_targets ipb::ipb-sink-console )
list(APPEND _cmake_import_check_files_for_ipb::ipb-sink-console "${_IMPORT_PREFIX}/lib/libipb-sink-console.1.0.0.dylib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
