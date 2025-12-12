#pragma once

/**
 * @file platform.hpp
 * @brief Centralized platform detection and OS abstraction
 *
 * This header provides:
 * - Compile-time platform detection
 * - OS-specific feature detection
 * - Architecture detection
 * - Compiler detection
 * - Runtime environment queries
 */

#include <cstdint>
#include <string_view>
#include <string>

// ============================================================================
// COMPILER DETECTION
// ============================================================================

#if defined(__clang__)
    #define IPB_COMPILER_CLANG 1
    #define IPB_COMPILER_NAME "Clang"
    #define IPB_COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#elif defined(__GNUC__) || defined(__GNUG__)
    #define IPB_COMPILER_GCC 1
    #define IPB_COMPILER_NAME "GCC"
    #define IPB_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#elif defined(_MSC_VER)
    #define IPB_COMPILER_MSVC 1
    #define IPB_COMPILER_NAME "MSVC"
    #define IPB_COMPILER_VERSION _MSC_VER
#elif defined(__INTEL_COMPILER)
    #define IPB_COMPILER_INTEL 1
    #define IPB_COMPILER_NAME "Intel"
    #define IPB_COMPILER_VERSION __INTEL_COMPILER
#else
    #define IPB_COMPILER_UNKNOWN 1
    #define IPB_COMPILER_NAME "Unknown"
    #define IPB_COMPILER_VERSION 0
#endif

// ============================================================================
// OPERATING SYSTEM DETECTION
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
    #define IPB_OS_WINDOWS 1
    #define IPB_OS_NAME "Windows"
    #if defined(_WIN64)
        #define IPB_OS_WINDOWS_64 1
    #else
        #define IPB_OS_WINDOWS_32 1
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #include <TargetConditionals.h>
    #define IPB_OS_APPLE 1
    #if TARGET_OS_IPHONE
        #define IPB_OS_IOS 1
        #define IPB_OS_NAME "iOS"
    #elif TARGET_OS_MAC
        #define IPB_OS_MACOS 1
        #define IPB_OS_NAME "macOS"
    #else
        #define IPB_OS_NAME "Apple"
    #endif
#elif defined(__linux__)
    #define IPB_OS_LINUX 1
    #define IPB_OS_NAME "Linux"
    #if defined(__ANDROID__)
        #define IPB_OS_ANDROID 1
        #undef IPB_OS_NAME
        #define IPB_OS_NAME "Android"
    #endif
#elif defined(__FreeBSD__)
    #define IPB_OS_FREEBSD 1
    #define IPB_OS_NAME "FreeBSD"
#elif defined(__OpenBSD__)
    #define IPB_OS_OPENBSD 1
    #define IPB_OS_NAME "OpenBSD"
#elif defined(__NetBSD__)
    #define IPB_OS_NETBSD 1
    #define IPB_OS_NAME "NetBSD"
#elif defined(__unix__)
    #define IPB_OS_UNIX 1
    #define IPB_OS_NAME "Unix"
#else
    #define IPB_OS_UNKNOWN 1
    #define IPB_OS_NAME "Unknown"
#endif

// POSIX detection
#if defined(IPB_OS_LINUX) || defined(IPB_OS_MACOS) || defined(IPB_OS_FREEBSD) || \
    defined(IPB_OS_OPENBSD) || defined(IPB_OS_NETBSD) || defined(IPB_OS_UNIX)
    #define IPB_OS_POSIX 1
#endif

// ============================================================================
// ARCHITECTURE DETECTION
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    #define IPB_ARCH_X86_64 1
    #define IPB_ARCH_NAME "x86_64"
    #define IPB_ARCH_BITS 64
#elif defined(__i386__) || defined(_M_IX86) || defined(__i686__)
    #define IPB_ARCH_X86 1
    #define IPB_ARCH_NAME "x86"
    #define IPB_ARCH_BITS 32
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define IPB_ARCH_ARM64 1
    #define IPB_ARCH_NAME "ARM64"
    #define IPB_ARCH_BITS 64
#elif defined(__arm__) || defined(_M_ARM)
    #define IPB_ARCH_ARM 1
    #define IPB_ARCH_NAME "ARM"
    #define IPB_ARCH_BITS 32
#elif defined(__riscv)
    #if __riscv_xlen == 64
        #define IPB_ARCH_RISCV64 1
        #define IPB_ARCH_NAME "RISC-V 64"
        #define IPB_ARCH_BITS 64
    #else
        #define IPB_ARCH_RISCV32 1
        #define IPB_ARCH_NAME "RISC-V 32"
        #define IPB_ARCH_BITS 32
    #endif
#elif defined(__powerpc64__) || defined(__ppc64__)
    #define IPB_ARCH_PPC64 1
    #define IPB_ARCH_NAME "PowerPC 64"
    #define IPB_ARCH_BITS 64
#elif defined(__powerpc__) || defined(__ppc__)
    #define IPB_ARCH_PPC 1
    #define IPB_ARCH_NAME "PowerPC"
    #define IPB_ARCH_BITS 32
#else
    #define IPB_ARCH_UNKNOWN 1
    #define IPB_ARCH_NAME "Unknown"
    #define IPB_ARCH_BITS 0
#endif

// Endianness detection
#if defined(__BYTE_ORDER__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define IPB_LITTLE_ENDIAN 1
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define IPB_BIG_ENDIAN 1
    #endif
#elif defined(IPB_ARCH_X86) || defined(IPB_ARCH_X86_64) || defined(IPB_ARCH_ARM) || defined(IPB_ARCH_ARM64)
    #define IPB_LITTLE_ENDIAN 1
#else
    // Assume little endian by default
    #define IPB_LITTLE_ENDIAN 1
#endif

// ============================================================================
// BUILD TYPE DETECTION
// ============================================================================

#if defined(NDEBUG) || defined(IPB_RELEASE)
    #define IPB_BUILD_RELEASE 1
    #define IPB_BUILD_TYPE "Release"
#else
    #define IPB_BUILD_DEBUG 1
    #define IPB_BUILD_TYPE "Debug"
#endif

// ============================================================================
// FEATURE DETECTION
// ============================================================================

// C++ version
#if __cplusplus >= 202302L
    #define IPB_CPP_VERSION 23
#elif __cplusplus >= 202002L
    #define IPB_CPP_VERSION 20
#elif __cplusplus >= 201703L
    #define IPB_CPP_VERSION 17
#elif __cplusplus >= 201402L
    #define IPB_CPP_VERSION 14
#elif __cplusplus >= 201103L
    #define IPB_CPP_VERSION 11
#else
    #define IPB_CPP_VERSION 0
#endif

// Thread support
#if defined(__cpp_lib_jthread) || (IPB_CPP_VERSION >= 20)
    #define IPB_HAS_JTHREAD 1
#endif

// Source location (C++20)
#if defined(__cpp_lib_source_location) || (IPB_CPP_VERSION >= 20 && !defined(IPB_COMPILER_MSVC))
    #define IPB_HAS_SOURCE_LOCATION 1
#endif

// Concepts (C++20)
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
    #define IPB_HAS_CONCEPTS 1
#endif

// Coroutines (C++20)
#if defined(__cpp_impl_coroutine) || (IPB_CPP_VERSION >= 20)
    #define IPB_HAS_COROUTINES 1
#endif

// Modules (C++20) - limited support
#if defined(__cpp_modules)
    #define IPB_HAS_MODULES 1
#endif

// std::format (C++20)
#if defined(__cpp_lib_format)
    #define IPB_HAS_STD_FORMAT 1
#endif

// std::span (C++20)
#if defined(__cpp_lib_span) || (IPB_CPP_VERSION >= 20)
    #define IPB_HAS_STD_SPAN 1
#endif

// std::expected (C++23)
#if defined(__cpp_lib_expected)
    #define IPB_HAS_STD_EXPECTED 1
#endif

// Ranges (C++20)
#if defined(__cpp_lib_ranges) || (IPB_CPP_VERSION >= 20)
    #define IPB_HAS_RANGES 1
#endif

// Three-way comparison (C++20)
#if defined(__cpp_impl_three_way_comparison) || (IPB_CPP_VERSION >= 20)
    #define IPB_HAS_SPACESHIP 1
#endif

// ============================================================================
// PLATFORM-SPECIFIC FEATURES
// ============================================================================

// Real-time scheduling support
#if defined(IPB_OS_LINUX)
    #define IPB_HAS_REALTIME_SCHED 1
    #define IPB_HAS_CPU_AFFINITY 1
    #define IPB_HAS_NUMA 1
#elif defined(IPB_OS_FREEBSD)
    #define IPB_HAS_REALTIME_SCHED 1
    #define IPB_HAS_CPU_AFFINITY 1
#elif defined(IPB_OS_MACOS)
    #define IPB_HAS_CPU_AFFINITY 1
#endif

// Memory mapping
#if defined(IPB_OS_POSIX)
    #define IPB_HAS_MMAP 1
#elif defined(IPB_OS_WINDOWS)
    #define IPB_HAS_VIRTUAL_ALLOC 1
#endif

// File system notifications
#if defined(IPB_OS_LINUX)
    #define IPB_HAS_INOTIFY 1
#elif defined(IPB_OS_MACOS) || defined(IPB_OS_FREEBSD)
    #define IPB_HAS_KQUEUE 1
#elif defined(IPB_OS_WINDOWS)
    #define IPB_HAS_DIRECTORY_CHANGES 1
#endif

// Signal handling
#if defined(IPB_OS_POSIX)
    #define IPB_HAS_POSIX_SIGNALS 1
    #define IPB_HAS_SIGACTION 1
#endif

// Daemonization
#if defined(IPB_OS_POSIX)
    #define IPB_HAS_FORK 1
    #define IPB_HAS_SETSID 1
#endif

// ============================================================================
// COMPILER ATTRIBUTES AND INTRINSICS
// ============================================================================

// Branch prediction hints
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define IPB_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define IPB_LIKELY(x)   (x)
    #define IPB_UNLIKELY(x) (x)
#endif

// Force inline
#if defined(IPB_COMPILER_MSVC)
    #define IPB_FORCE_INLINE __forceinline
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_FORCE_INLINE __attribute__((always_inline)) inline
#else
    #define IPB_FORCE_INLINE inline
#endif

// No inline
#if defined(IPB_COMPILER_MSVC)
    #define IPB_NO_INLINE __declspec(noinline)
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_NO_INLINE __attribute__((noinline))
#else
    #define IPB_NO_INLINE
#endif

// Unreachable code
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_UNREACHABLE() __builtin_unreachable()
#elif defined(IPB_COMPILER_MSVC)
    #define IPB_UNREACHABLE() __assume(0)
#else
    #define IPB_UNREACHABLE() ((void)0)
#endif

// Function signature
#if defined(IPB_COMPILER_MSVC)
    #define IPB_FUNCTION_SIGNATURE __FUNCSIG__
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#else
    #define IPB_FUNCTION_SIGNATURE __func__
#endif

// Deprecated
#if IPB_CPP_VERSION >= 14
    #define IPB_DEPRECATED(msg) [[deprecated(msg)]]
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(IPB_COMPILER_MSVC)
    #define IPB_DEPRECATED(msg) __declspec(deprecated(msg))
#else
    #define IPB_DEPRECATED(msg)
#endif

// No discard
#if IPB_CPP_VERSION >= 17
    #define IPB_NODISCARD [[nodiscard]]
    #define IPB_NODISCARD_MSG(msg) [[nodiscard(msg)]]
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_NODISCARD __attribute__((warn_unused_result))
    #define IPB_NODISCARD_MSG(msg) __attribute__((warn_unused_result))
#else
    #define IPB_NODISCARD
    #define IPB_NODISCARD_MSG(msg)
#endif

// Maybe unused
#if IPB_CPP_VERSION >= 17
    #define IPB_MAYBE_UNUSED [[maybe_unused]]
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_MAYBE_UNUSED __attribute__((unused))
#else
    #define IPB_MAYBE_UNUSED
#endif

// Fallthrough
#if IPB_CPP_VERSION >= 17
    #define IPB_FALLTHROUGH [[fallthrough]]
#elif defined(IPB_COMPILER_GCC) && IPB_COMPILER_VERSION >= 70000
    #define IPB_FALLTHROUGH __attribute__((fallthrough))
#elif defined(IPB_COMPILER_CLANG)
    #define IPB_FALLTHROUGH [[clang::fallthrough]]
#else
    #define IPB_FALLTHROUGH ((void)0)
#endif

// Export/Import for shared libraries
#if defined(IPB_OS_WINDOWS)
    #if defined(IPB_BUILDING_SHARED)
        #define IPB_API __declspec(dllexport)
    #elif defined(IPB_USING_SHARED)
        #define IPB_API __declspec(dllimport)
    #else
        #define IPB_API
    #endif
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #if defined(IPB_BUILDING_SHARED)
        #define IPB_API __attribute__((visibility("default")))
    #else
        #define IPB_API
    #endif
#else
    #define IPB_API
#endif

// Thread local storage
#if IPB_CPP_VERSION >= 11
    #define IPB_THREAD_LOCAL thread_local
#elif defined(IPB_COMPILER_MSVC)
    #define IPB_THREAD_LOCAL __declspec(thread)
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_THREAD_LOCAL __thread
#else
    #define IPB_THREAD_LOCAL
#endif

// Alignment
#if IPB_CPP_VERSION >= 11
    #define IPB_ALIGNAS(n) alignas(n)
    #define IPB_ALIGNOF(T) alignof(T)
#elif defined(IPB_COMPILER_MSVC)
    #define IPB_ALIGNAS(n) __declspec(align(n))
    #define IPB_ALIGNOF(T) __alignof(T)
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_ALIGNAS(n) __attribute__((aligned(n)))
    #define IPB_ALIGNOF(T) __alignof__(T)
#else
    #define IPB_ALIGNAS(n)
    #define IPB_ALIGNOF(T) sizeof(T)
#endif

// ============================================================================
// HOT PATH OPTIMIZATION MACROS
// ============================================================================

// Prefetch hints for data locality optimization
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    // Read prefetch (data will be read)
    #define IPB_PREFETCH_READ(addr) __builtin_prefetch((addr), 0, 3)
    // Write prefetch (data will be written)
    #define IPB_PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
    // Non-temporal prefetch (data won't be reused soon)
    #define IPB_PREFETCH_NTA(addr) __builtin_prefetch((addr), 0, 0)
#elif defined(IPB_COMPILER_MSVC) && defined(_M_X64)
    #include <intrin.h>
    #define IPB_PREFETCH_READ(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
    #define IPB_PREFETCH_WRITE(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
    #define IPB_PREFETCH_NTA(addr) _mm_prefetch((const char*)(addr), _MM_HINT_NTA)
#else
    #define IPB_PREFETCH_READ(addr) ((void)(addr))
    #define IPB_PREFETCH_WRITE(addr) ((void)(addr))
    #define IPB_PREFETCH_NTA(addr) ((void)(addr))
#endif

// Restrict pointer hint (no aliasing)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_RESTRICT __restrict__
#elif defined(IPB_COMPILER_MSVC)
    #define IPB_RESTRICT __restrict
#else
    #define IPB_RESTRICT
#endif

// Hot function (frequently called, optimize for speed)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_HOT __attribute__((hot))
#else
    #define IPB_HOT
#endif

// Cold function (rarely called, optimize for size)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_COLD __attribute__((cold))
#else
    #define IPB_COLD
#endif

// Pure function (no side effects, depends only on args and global state)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_PURE __attribute__((pure))
#else
    #define IPB_PURE
#endif

// Const function (no side effects, depends only on args)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_CONST __attribute__((const))
#else
    #define IPB_CONST
#endif

// Assume pointer is aligned (for SIMD optimizations)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_ASSUME_ALIGNED(ptr, alignment) __builtin_assume_aligned((ptr), (alignment))
#else
    #define IPB_ASSUME_ALIGNED(ptr, alignment) (ptr)
#endif

// Assume condition is true (helps optimizer)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_ASSUME(cond) do { if (!(cond)) __builtin_unreachable(); } while(0)
#elif defined(IPB_COMPILER_MSVC)
    #define IPB_ASSUME(cond) __assume(cond)
#else
    #define IPB_ASSUME(cond) ((void)(cond))
#endif

// CPU pause instruction (for spin loops, reduces power and contention)
#if defined(IPB_ARCH_X86_64) || defined(IPB_ARCH_X86)
    #if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
        #define IPB_CPU_PAUSE() __builtin_ia32_pause()
    #elif defined(IPB_COMPILER_MSVC)
        #define IPB_CPU_PAUSE() _mm_pause()
    #else
        #define IPB_CPU_PAUSE() ((void)0)
    #endif
#elif defined(IPB_ARCH_ARM64)
    #if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
        #define IPB_CPU_PAUSE() __asm__ __volatile__("yield" ::: "memory")
    #else
        #define IPB_CPU_PAUSE() ((void)0)
    #endif
#else
    #define IPB_CPU_PAUSE() ((void)0)
#endif

// Memory barrier (compiler barrier, not CPU barrier)
#if defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    #define IPB_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#elif defined(IPB_COMPILER_MSVC)
    #define IPB_COMPILER_BARRIER() _ReadWriteBarrier()
#else
    #define IPB_COMPILER_BARRIER() ((void)0)
#endif

// Cache line size (architecture-dependent)
#if defined(IPB_ARCH_X86) || defined(IPB_ARCH_X86_64)
    #define IPB_CACHE_LINE_SIZE 64
#elif defined(IPB_ARCH_ARM64)
    #define IPB_CACHE_LINE_SIZE 64
#elif defined(IPB_ARCH_ARM)
    #define IPB_CACHE_LINE_SIZE 32
#else
    #define IPB_CACHE_LINE_SIZE 64
#endif

// Macro for cache-line aligned structures
#define IPB_CACHE_ALIGNED IPB_ALIGNAS(IPB_CACHE_LINE_SIZE)

namespace ipb::common::platform {

// ============================================================================
// Runtime Platform Information
// ============================================================================

/**
 * @brief Platform identification structure
 */
struct PlatformInfo {
    std::string_view os_name;
    std::string_view arch_name;
    std::string_view compiler_name;
    uint32_t compiler_version;
    uint8_t arch_bits;
    bool is_debug;
    bool is_little_endian;
    uint32_t cpp_version;
};

/**
 * @brief Get compile-time platform information
 */
constexpr PlatformInfo get_platform_info() noexcept {
    return PlatformInfo{
        .os_name = IPB_OS_NAME,
        .arch_name = IPB_ARCH_NAME,
        .compiler_name = IPB_COMPILER_NAME,
        .compiler_version = IPB_COMPILER_VERSION,
        .arch_bits = IPB_ARCH_BITS,
#ifdef IPB_BUILD_DEBUG
        .is_debug = true,
#else
        .is_debug = false,
#endif
#ifdef IPB_LITTLE_ENDIAN
        .is_little_endian = true,
#else
        .is_little_endian = false,
#endif
        .cpp_version = IPB_CPP_VERSION
    };
}

/**
 * @brief Feature availability flags
 */
struct FeatureFlags {
    bool has_realtime_sched : 1;
    bool has_cpu_affinity : 1;
    bool has_numa : 1;
    bool has_mmap : 1;
    bool has_source_location : 1;
    bool has_concepts : 1;
    bool has_coroutines : 1;
    bool has_std_format : 1;
    bool has_ranges : 1;
    bool has_jthread : 1;
};

/**
 * @brief Get compile-time feature flags
 */
constexpr FeatureFlags get_feature_flags() noexcept {
    return FeatureFlags{
#ifdef IPB_HAS_REALTIME_SCHED
        .has_realtime_sched = true,
#else
        .has_realtime_sched = false,
#endif
#ifdef IPB_HAS_CPU_AFFINITY
        .has_cpu_affinity = true,
#else
        .has_cpu_affinity = false,
#endif
#ifdef IPB_HAS_NUMA
        .has_numa = true,
#else
        .has_numa = false,
#endif
#ifdef IPB_HAS_MMAP
        .has_mmap = true,
#else
        .has_mmap = false,
#endif
#ifdef IPB_HAS_SOURCE_LOCATION
        .has_source_location = true,
#else
        .has_source_location = false,
#endif
#ifdef IPB_HAS_CONCEPTS
        .has_concepts = true,
#else
        .has_concepts = false,
#endif
#ifdef IPB_HAS_COROUTINES
        .has_coroutines = true,
#else
        .has_coroutines = false,
#endif
#ifdef IPB_HAS_STD_FORMAT
        .has_std_format = true,
#else
        .has_std_format = false,
#endif
#ifdef IPB_HAS_RANGES
        .has_ranges = true,
#else
        .has_ranges = false,
#endif
#ifdef IPB_HAS_JTHREAD
        .has_jthread = true,
#else
        .has_jthread = false,
#endif
    };
}

// ============================================================================
// Runtime Environment Queries
// ============================================================================

/**
 * @brief Get number of CPU cores
 */
IPB_API uint32_t get_cpu_count() noexcept;

/**
 * @brief Get total system memory in bytes
 */
IPB_API uint64_t get_total_memory() noexcept;

/**
 * @brief Get available system memory in bytes
 */
IPB_API uint64_t get_available_memory() noexcept;

/**
 * @brief Get system page size in bytes
 */
IPB_API size_t get_page_size() noexcept;

/**
 * @brief Get hostname
 */
IPB_API std::string get_hostname();

/**
 * @brief Get current process ID
 */
IPB_API uint64_t get_process_id() noexcept;

/**
 * @brief Get current thread ID
 */
IPB_API uint64_t get_thread_id() noexcept;

/**
 * @brief Check if running as root/administrator
 */
IPB_API bool is_elevated() noexcept;

/**
 * @brief Get environment variable value
 * @return Empty string if not found
 */
IPB_API std::string get_env(std::string_view name);

/**
 * @brief Set environment variable
 * @return true on success
 */
IPB_API bool set_env(std::string_view name, std::string_view value);

// ============================================================================
// CPU Feature Detection (Runtime)
// ============================================================================

/**
 * @brief CPU feature flags detected at runtime
 */
struct CpuFeatures {
    // x86/x86_64
    bool has_sse : 1;
    bool has_sse2 : 1;
    bool has_sse3 : 1;
    bool has_ssse3 : 1;
    bool has_sse41 : 1;
    bool has_sse42 : 1;
    bool has_avx : 1;
    bool has_avx2 : 1;
    bool has_avx512 : 1;
    bool has_aes : 1;
    bool has_sha : 1;

    // ARM
    bool has_neon : 1;
    bool has_crc32 : 1;
    bool has_crypto : 1;
};

/**
 * @brief Detect CPU features at runtime
 */
IPB_API CpuFeatures detect_cpu_features() noexcept;

} // namespace ipb::common::platform
