#include <ipb/common/platform.hpp>

#include <cstdlib>
#include <cstring>
#include <thread>

// Platform-specific includes
#if defined(IPB_OS_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <intrin.h>
#include <windows.h>
#elif defined(IPB_OS_POSIX)
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(IPB_OS_LINUX)
#include <sched.h>
#include <sys/sysinfo.h>
#elif defined(IPB_OS_MACOS)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(IPB_OS_FREEBSD)
#include <sys/sysctl.h>
#endif
#endif

// For CPUID on x86
#if (defined(IPB_ARCH_X86) || defined(IPB_ARCH_X86_64)) && !defined(IPB_COMPILER_MSVC)
#include <cpuid.h>
#endif

namespace ipb::common::platform {

// ============================================================================
// CPU Count
// ============================================================================

uint32_t get_cpu_count() noexcept {
    // Use C++ standard way first
    unsigned int count = std::thread::hardware_concurrency();
    if (count > 0) {
        return count;
    }

#if defined(IPB_OS_WINDOWS)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<uint32_t>(si.dwNumberOfProcessors);
#elif defined(IPB_OS_LINUX)
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return nprocs > 0 ? static_cast<uint32_t>(nprocs) : 1;
#elif defined(IPB_OS_MACOS) || defined(IPB_OS_FREEBSD)
    int mib[2] = {CTL_HW, HW_NCPU};
    int ncpu   = 1;
    size_t len = sizeof(ncpu);
    sysctl(mib, 2, &ncpu, &len, nullptr, 0);
    return static_cast<uint32_t>(ncpu);
#else
    return 1;
#endif
}

// ============================================================================
// Memory Information
// ============================================================================

uint64_t get_total_memory() noexcept {
#if defined(IPB_OS_WINDOWS)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullTotalPhys;
    }
    return 0;
#elif defined(IPB_OS_LINUX)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return static_cast<uint64_t>(si.totalram) * si.mem_unit;
    }
    return 0;
#elif defined(IPB_OS_MACOS)
    int mib[2]       = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len       = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
        return memsize;
    }
    return 0;
#elif defined(IPB_OS_FREEBSD)
    int mib[2]            = {CTL_HW, HW_PHYSMEM};
    unsigned long physmem = 0;
    size_t len            = sizeof(physmem);
    if (sysctl(mib, 2, &physmem, &len, nullptr, 0) == 0) {
        return physmem;
    }
    return 0;
#else
    return 0;
#endif
}

uint64_t get_available_memory() noexcept {
#if defined(IPB_OS_WINDOWS)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return memInfo.ullAvailPhys;
    }
    return 0;
#elif defined(IPB_OS_LINUX)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return static_cast<uint64_t>(si.freeram) * si.mem_unit;
    }
    return 0;
#elif defined(IPB_OS_MACOS)
    mach_port_t host_port = mach_host_self();
    vm_size_t page_size;
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);

    host_page_size(host_port, &page_size);
    if (host_statistics64(host_port, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm_stats),
                          &count) == KERN_SUCCESS) {
        return static_cast<uint64_t>(vm_stats.free_count) * page_size;
    }
    return 0;
#else
    return 0;
#endif
}

// ============================================================================
// Page Size
// ============================================================================

size_t get_page_size() noexcept {
#if defined(IPB_OS_WINDOWS)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#elif defined(IPB_OS_POSIX)
    long page_size = sysconf(_SC_PAGESIZE);
    return page_size > 0 ? static_cast<size_t>(page_size) : 4096;
#else
    return 4096;
#endif
}

// ============================================================================
// Hostname
// ============================================================================

std::string get_hostname() {
#if defined(IPB_OS_WINDOWS)
    char hostname[256];
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        return std::string(hostname);
    }
    return "localhost";
#elif defined(IPB_OS_POSIX)
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        return std::string(hostname);
    }
    return "localhost";
#else
    return "localhost";
#endif
}

// ============================================================================
// Process and Thread IDs
// ============================================================================

uint64_t get_process_id() noexcept {
#if defined(IPB_OS_WINDOWS)
    return static_cast<uint64_t>(GetCurrentProcessId());
#elif defined(IPB_OS_POSIX)
    return static_cast<uint64_t>(getpid());
#else
    return 0;
#endif
}

uint64_t get_thread_id() noexcept {
#if defined(IPB_OS_WINDOWS)
    return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(IPB_OS_LINUX)
    return static_cast<uint64_t>(pthread_self());
#elif defined(IPB_OS_MACOS)
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#elif defined(IPB_OS_POSIX)
    return static_cast<uint64_t>(pthread_self());
#else
    return 0;
#endif
}

// ============================================================================
// Elevation Check
// ============================================================================

bool is_elevated() noexcept {
#if defined(IPB_OS_WINDOWS)
    BOOL fRet     = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            fRet = Elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return fRet != FALSE;
#elif defined(IPB_OS_POSIX)
    return geteuid() == 0;
#else
    return false;
#endif
}

// ============================================================================
// Environment Variables
// ============================================================================

std::string get_env(std::string_view name) {
    std::string name_str(name);
#if defined(IPB_OS_WINDOWS)
    char buffer[32767];
    DWORD result = GetEnvironmentVariableA(name_str.c_str(), buffer, sizeof(buffer));
    if (result > 0 && result < sizeof(buffer)) {
        return std::string(buffer);
    }
    return {};
#else
    const char* value = std::getenv(name_str.c_str());
    return value ? std::string(value) : std::string{};
#endif
}

bool set_env(std::string_view name, std::string_view value) {
    std::string name_str(name);
    std::string value_str(value);
#if defined(IPB_OS_WINDOWS)
    return SetEnvironmentVariableA(name_str.c_str(), value_str.c_str()) != 0;
#elif defined(IPB_OS_POSIX)
    return setenv(name_str.c_str(), value_str.c_str(), 1) == 0;
#else
    return false;
#endif
}

// ============================================================================
// CPU Feature Detection
// ============================================================================

#if defined(IPB_ARCH_X86) || defined(IPB_ARCH_X86_64)
namespace {

struct CpuidResult {
    uint32_t eax, ebx, ecx, edx;
};

CpuidResult cpuid(uint32_t leaf, uint32_t subleaf = 0) {
    CpuidResult result = {0, 0, 0, 0};
#if defined(IPB_COMPILER_MSVC)
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    result.eax = regs[0];
    result.ebx = regs[1];
    result.ecx = regs[2];
    result.edx = regs[3];
#elif defined(IPB_COMPILER_GCC) || defined(IPB_COMPILER_CLANG)
    __cpuid_count(leaf, subleaf, result.eax, result.ebx, result.ecx, result.edx);
#endif
    return result;
}

}  // anonymous namespace
#endif

CpuFeatures detect_cpu_features() noexcept {
    CpuFeatures features = {};

#if defined(IPB_ARCH_X86) || defined(IPB_ARCH_X86_64)
    // Get max leaf
    auto leaf0        = cpuid(0);
    uint32_t max_leaf = leaf0.eax;

    if (max_leaf >= 1) {
        auto leaf1 = cpuid(1);

        // EDX flags
        features.has_sse  = (leaf1.edx & (1 << 25)) != 0;
        features.has_sse2 = (leaf1.edx & (1 << 26)) != 0;

        // ECX flags
        features.has_sse3  = (leaf1.ecx & (1 << 0)) != 0;
        features.has_ssse3 = (leaf1.ecx & (1 << 9)) != 0;
        features.has_sse41 = (leaf1.ecx & (1 << 19)) != 0;
        features.has_sse42 = (leaf1.ecx & (1 << 20)) != 0;
        features.has_aes   = (leaf1.ecx & (1 << 25)) != 0;
        features.has_avx   = (leaf1.ecx & (1 << 28)) != 0;
    }

    if (max_leaf >= 7) {
        auto leaf7 = cpuid(7);

        // EBX flags
        features.has_avx2   = (leaf7.ebx & (1 << 5)) != 0;
        features.has_avx512 = (leaf7.ebx & (1 << 16)) != 0;  // AVX-512F
        features.has_sha    = (leaf7.ebx & (1 << 29)) != 0;
    }

#elif defined(IPB_ARCH_ARM64)
    // ARM64 always has NEON
    features.has_neon = true;

#if defined(IPB_OS_LINUX)
    // On Linux, we could check /proc/cpuinfo or use getauxval
    // For simplicity, assume common features are available
    features.has_crc32  = true;
    features.has_crypto = true;
#elif defined(IPB_OS_MACOS)
    // Apple Silicon has all these features
    features.has_crc32  = true;
    features.has_crypto = true;
#endif

#elif defined(IPB_ARCH_ARM)
// Check for NEON at runtime on 32-bit ARM
#if defined(IPB_OS_LINUX)
    // Would need to check /proc/cpuinfo
    features.has_neon = false;  // Conservative default
#endif
#endif

    return features;
}

}  // namespace ipb::common::platform
