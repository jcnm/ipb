/**
 * @file rt_primitives.cpp
 * @brief Real-time primitives - legacy implementation file
 *
 * Real-time primitives have been moved to endpoint.hpp as template classes.
 * This file provides backwards compatibility and additional utility functions.
 */

#include <ipb/common/endpoint.hpp>

#include <chrono>
#include <thread>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace ipb::common::rt {

/**
 * @brief Lock all current and future memory pages
 */
bool lock_memory() noexcept {
#ifdef __linux__
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
#else
    return true;
#endif
}

/**
 * @brief Unlock all memory pages
 */
bool unlock_memory() noexcept {
#ifdef __linux__
    return munlockall() == 0;
#else
    return true;
#endif
}

/**
 * @brief Precise sleep with busy-wait for short durations
 */
void precise_sleep(std::chrono::nanoseconds duration) noexcept {
    auto start = std::chrono::high_resolution_clock::now();
    auto end   = start + duration;

    // For very short durations, use busy wait
    if (duration < std::chrono::microseconds(100)) {
        while (std::chrono::high_resolution_clock::now() < end) {
            // Busy wait with pause instruction for CPU efficiency
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
        return;
    }

    // For longer durations, use a combination of sleep and busy wait
    auto sleep_duration = duration - std::chrono::microseconds(50);
    if (sleep_duration > std::chrono::nanoseconds(0)) {
        std::this_thread::sleep_for(sleep_duration);
    }

    // Busy wait for the remaining time
    while (std::chrono::high_resolution_clock::now() < end) {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#endif
    }
}

/**
 * @brief Get CPU cycle counter (TSC on x86)
 */
uint64_t get_cpu_cycles() noexcept {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__aarch64__)
    uint64_t cycles;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(cycles));
    return cycles;
#else
    // Fallback to high resolution clock
    auto now = std::chrono::high_resolution_clock::now();
    return static_cast<uint64_t>(now.time_since_epoch().count());
#endif
}

/**
 * @brief Estimate CPU frequency in GHz
 */
double get_cpu_frequency_ghz() noexcept {
    // Simple estimation - measure cycles over a known time period
    auto start_cycles = get_cpu_cycles();
    auto start_time   = std::chrono::high_resolution_clock::now();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto end_cycles = get_cpu_cycles();
    auto end_time   = std::chrono::high_resolution_clock::now();

    auto duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    auto cycles_diff = end_cycles - start_cycles;

    return static_cast<double>(cycles_diff) / duration_ns;
}

/**
 * @brief Set thread to run on a specific CPU using affinity mask
 */
bool set_thread_affinity(std::thread& thread, const std::vector<int>& cpu_cores) noexcept {
#ifdef __linux__
    pthread_t native_handle = thread.native_handle();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int core : cpu_cores) {
        if (core >= 0 && core < CPU_SETSIZE) {
            CPU_SET(core, &cpuset);
        }
    }

    return pthread_setaffinity_np(native_handle, sizeof(cpu_set_t), &cpuset) == 0;
#else
    (void)thread;
    (void)cpu_cores;
    return true;
#endif
}

/**
 * @brief Legacy priority enum for backwards compatibility
 */
enum class LegacyThreadPriority { LOW, NORMAL, HIGH, REALTIME };

/**
 * @brief Set thread priority (legacy function)
 */
bool set_thread_priority(std::thread& thread, LegacyThreadPriority priority) noexcept {
#ifdef __linux__
    pthread_t native_handle = thread.native_handle();

    struct sched_param param;
    int policy;

    switch (priority) {
        case LegacyThreadPriority::LOW:
            policy               = SCHED_OTHER;
            param.sched_priority = 0;
            break;
        case LegacyThreadPriority::NORMAL:
            policy               = SCHED_OTHER;
            param.sched_priority = 0;
            break;
        case LegacyThreadPriority::HIGH:
            policy               = SCHED_FIFO;
            param.sched_priority = 50;
            break;
        case LegacyThreadPriority::REALTIME:
            policy               = SCHED_FIFO;
            param.sched_priority = 99;
            break;
        default:
            return false;
    }

    return pthread_setschedparam(native_handle, policy, &param) == 0;
#else
    (void)thread;
    (void)priority;
    return true;
#endif
}

}  // namespace ipb::common::rt
