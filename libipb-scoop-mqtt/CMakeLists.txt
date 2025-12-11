cmake_minimum_required(VERSION 3.20)
project(libipb-scoop-mqtt VERSION 1.0.0 LANGUAGES CXX)

# Library
add_library(ipb-scoop-mqtt
    src/mqtt_scoop.cpp
)

add_library(ipb::scoop-mqtt ALIAS ipb-scoop-mqtt)

target_include_directories(ipb-scoop-mqtt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ipb-scoop-mqtt
    PUBLIC
        ipb::common
        ipb::transport-mqtt
    PRIVATE
        jsoncpp_lib
)

target_compile_features(ipb-scoop-mqtt PUBLIC cxx_std_20)

# Installation
include(GNUInstallDirs)

install(TARGETS ipb-scoop-mqtt
    EXPORT ipb-scoop-mqtt-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ipb-scoop-mqtt-targets
    FILE ipb-scoop-mqtt-targets.cmake
    NAMESPACE ipb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-scoop-mqtt
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipb-scoop-mqtt-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-scoop-mqtt-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-scoop-mqtt
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-scoop-mqtt-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-scoop-mqtt-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ipb-scoop-mqtt-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ipb-scoop-mqtt
)
