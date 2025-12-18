#pragma once

/**
 * @file realtime_optimizations.hpp
 * @brief Compile-time and runtime optimizations for hard real-time performance
 *
 * Provides a unified header for all IPB real-time optimizations:
 * - Memory configuration profiles
 * - Lock-free data structures
 * - Fixed-size string types
 * - Cached pattern matching
 * - CPU optimization hints
 *
 * Target: <250μs P99 latency, <500MB memory footprint
 */

#include <ipb/common/cached_pattern_matcher.hpp>
#include <ipb/common/fixed_string.hpp>
#include <ipb/common/lockfree_task_queue.hpp>
#include <ipb/common/memory_config.hpp>
#include <ipb/common/platform.hpp>

namespace ipb::common::rt {

// ============================================================================
// REAL-TIME INITIALIZATION
// ============================================================================

/**
 * @brief Initialize IPB with optimal settings for target platform
 *
 * Call this before any IPB component initialization.
 *
 * @param profile Memory profile (AUTO_DETECT recommended)
 * @param max_memory_mb Optional memory limit in MB (0 = no limit)
 * @return Estimated memory footprint in bytes
 */
inline size_t initialize(MemoryProfile profile = MemoryProfile::AUTO_DETECT,
                         size_t max_memory_mb = 0) noexcept {
    // Set memory profile
    GlobalMemoryConfig::set_profile(profile);

    // Apply memory limit if specified
    if (max_memory_mb > 0) {
        GlobalMemoryConfig::set_memory_limit(max_memory_mb);
    }

    // Pre-warm pattern cache
    PatternCache::global();

    return GlobalMemoryConfig::instance().estimated_footprint();
}

/**
 * @brief Initialize with explicit configuration
 */
inline size_t initialize(const MemoryConfig& config) noexcept {
    GlobalMemoryConfig::set(config);
    PatternCache::global();
    return config.estimated_footprint();
}

// ============================================================================
// REAL-TIME HELPERS
// ============================================================================

/**
 * @brief CPU yield for spin loops (power-efficient)
 */
inline void cpu_relax() noexcept {
    IPB_CPU_PAUSE();
}

/**
 * @brief Spin loop with exponential backoff
 *
 * @param condition Lambda returning true to continue spinning
 * @param max_spins Maximum spin iterations before yielding
 */
template <typename Condition>
inline void spin_wait(Condition&& condition, size_t max_spins = 1000) noexcept {
    size_t spins = 0;
    while (condition()) {
        if (spins < max_spins) {
            // Spin with CPU pause
            for (size_t i = 0; i < (1 << std::min(spins / 100, size_t(4))); ++i) {
                IPB_CPU_PAUSE();
            }
            ++spins;
        } else {
            // Yield to OS scheduler
            std::this_thread::yield();
            spins = max_spins / 2;  // Reset partially
        }
    }
}

/**
 * @brief Memory prefetch hint for data that will be read
 */
template <typename T>
inline void prefetch_read(const T* ptr) noexcept {
    IPB_PREFETCH_READ(ptr);
}

/**
 * @brief Memory prefetch hint for data that will be written
 */
template <typename T>
inline void prefetch_write(T* ptr) noexcept {
    IPB_PREFETCH_WRITE(ptr);
}

/**
 * @brief Prevent compiler reordering across this point
 */
inline void compiler_barrier() noexcept {
    IPB_COMPILER_BARRIER();
}

// ============================================================================
// ALLOCATION HINTS
// ============================================================================

/**
 * @brief Aligned allocation for cache-line optimization
 */
template <typename T, size_t Alignment = IPB_CACHE_LINE_SIZE>
T* aligned_alloc() {
    void* ptr = ::operator new(sizeof(T), std::align_val_t{Alignment});
    return new (ptr) T();
}

/**
 * @brief Aligned deallocation
 */
template <typename T, size_t Alignment = IPB_CACHE_LINE_SIZE>
void aligned_free(T* ptr) noexcept {
    if (ptr) {
        ptr->~T();
        ::operator delete(ptr, std::align_val_t{Alignment});
    }
}

/**
 * @brief RAII wrapper for cache-aligned objects
 */
template <typename T, size_t Alignment = IPB_CACHE_LINE_SIZE>
class AlignedPtr {
public:
    AlignedPtr() : ptr_(aligned_alloc<T, Alignment>()) {}

    template <typename... Args>
    explicit AlignedPtr(Args&&... args)
        : ptr_(new (::operator new(sizeof(T), std::align_val_t{Alignment}))
                   T(std::forward<Args>(args)...)) {}

    ~AlignedPtr() { aligned_free<T, Alignment>(ptr_); }

    // Non-copyable
    AlignedPtr(const AlignedPtr&) = delete;
    AlignedPtr& operator=(const AlignedPtr&) = delete;

    // Movable
    AlignedPtr(AlignedPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    AlignedPtr& operator=(AlignedPtr&& other) noexcept {
        if (this != &other) {
            aligned_free<T, Alignment>(ptr_);
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() noexcept { return ptr_; }
    const T* get() const noexcept { return ptr_; }
    T* operator->() noexcept { return ptr_; }
    const T* operator->() const noexcept { return ptr_; }
    T& operator*() noexcept { return *ptr_; }
    const T& operator*() const noexcept { return *ptr_; }

private:
    T* ptr_;
};

// ============================================================================
// PERFORMANCE MONITORING
// ============================================================================

/**
 * @brief High-resolution timestamp for latency measurement
 */
inline int64_t timestamp_ns() noexcept {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

/**
 * @brief Latency measurement helper
 */
class LatencyMeasure {
public:
    LatencyMeasure() noexcept : start_(timestamp_ns()) {}

    /// Get elapsed time in nanoseconds
    int64_t elapsed_ns() const noexcept {
        return timestamp_ns() - start_;
    }

    /// Get elapsed time in microseconds
    double elapsed_us() const noexcept {
        return static_cast<double>(elapsed_ns()) / 1000.0;
    }

    /// Reset the timer
    void reset() noexcept {
        start_ = timestamp_ns();
    }

private:
    int64_t start_;
};

/**
 * @brief RAII latency tracker with callback
 */
template <typename Callback>
class ScopedLatency {
public:
    explicit ScopedLatency(Callback&& cb) noexcept
        : callback_(std::forward<Callback>(cb)), start_(timestamp_ns()) {}

    ~ScopedLatency() {
        callback_(timestamp_ns() - start_);
    }

private:
    Callback callback_;
    int64_t start_;
};

/// Helper to create scoped latency tracker
template <typename Callback>
ScopedLatency<Callback> measure_latency(Callback&& cb) {
    return ScopedLatency<Callback>(std::forward<Callback>(cb));
}

// ============================================================================
// CONFIGURATION SUMMARY
// ============================================================================

/**
 * @brief Get current optimization configuration summary
 */
inline std::string get_config_summary() {
    const auto& cfg = GlobalMemoryConfig::instance();

    std::string summary;
    summary.reserve(512);

    summary += "IPB Real-Time Configuration:\n";
    summary += "  Memory Profile: ";

    if (cfg.scheduler_max_queue_size <= 256) {
        summary += "EMBEDDED";
    } else if (cfg.scheduler_max_queue_size <= 1000) {
        summary += "IOT";
    } else if (cfg.scheduler_max_queue_size <= 5000) {
        summary += "EDGE";
    } else if (cfg.scheduler_max_queue_size <= 10000) {
        summary += "STANDARD";
    } else {
        summary += "HIGH_PERF";
    }

    summary += "\n  Estimated Footprint: ";
    summary += std::to_string(cfg.estimated_footprint_mb());
    summary += " MB\n";

    summary += "  Scheduler Queue: ";
    summary += std::to_string(cfg.scheduler_max_queue_size);
    summary += "\n  Message Channels: ";
    summary += std::to_string(cfg.message_bus_max_channels);
    summary += " x ";
    summary += std::to_string(cfg.message_bus_buffer_size);
    summary += " buffer\n";

    auto cache_stats = PatternCache::global().stats();
    summary += "  Pattern Cache: ";
    summary += std::to_string(cache_stats.size);
    summary += " entries (";
    summary += std::to_string(static_cast<int>(cache_stats.hit_rate()));
    summary += "% hit rate)\n";

    return summary;
}

}  // namespace ipb::common::rt

// ============================================================================
// CONVENIENCE NAMESPACE ALIAS
// ============================================================================

namespace ipb::rt = ipb::common::rt;
