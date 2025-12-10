#pragma once

// IPB Build Information
// Generated automatically by CMake

#define IPB_VERSION_MAJOR 1
#define IPB_VERSION_MINOR 4
#define IPB_VERSION_PATCH 0
#define IPB_VERSION_STRING "1.4.0"

#define IPB_BUILD_TYPE "Release"
#define IPB_COMPILER_ID "AppleClang"
#define IPB_COMPILER_VERSION "17.0.0.17000310"
#define IPB_SYSTEM_NAME "Darwin"
#define IPB_SYSTEM_PROCESSOR "arm64"

// Build timestamp
#define IPB_BUILD_TIMESTAMP ""

// Component availability
#define IPB_HAS_CONSOLE_SINK 0
#define IPB_HAS_SYSLOG_SINK 0
#define IPB_HAS_MQTT_SINK 0
#define IPB_HAS_ROUTER 0
#define IPB_HAS_GATE 0

// Feature flags
#define IPB_ENABLE_OPTIMIZATIONS 0
#define IPB_ENABLE_LTO 0
#define IPB_ENABLE_SANITIZERS 0
#define IPB_ENABLE_COVERAGE 0

namespace ipb {
namespace build_info {

constexpr const char* version() { return IPB_VERSION_STRING; }
constexpr const char* build_type() { return IPB_BUILD_TYPE; }
constexpr const char* compiler() { return IPB_COMPILER_ID; }
constexpr const char* system() { return IPB_SYSTEM_NAME; }

} // namespace build_info
} // namespace ipb

