#pragma once

/**
 * @file memory_config.hpp
 * @brief Configurable memory profiles for different target environments
 *
 * Provides compile-time and runtime configurable memory settings to adapt
 * IPB to different target platforms:
 * - Embedded systems (< 64MB RAM)
 * - IoT devices (64-256MB RAM)
 * - Edge devices (256MB-1GB RAM)
 * - Standard servers (1-8GB RAM)
 * - High-performance servers (8GB+ RAM)
 *
 * Usage:
 * - Define IPB_MEMORY_PROFILE at compile time to select profile
 * - Or use MemoryConfig::create_for_available_memory() at runtime
 */

#include <ipb/common/platform.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ipb::common {

// ============================================================================
// MEMORY PROFILE SELECTION
// ============================================================================

/**
 * @brief Memory profile presets
 */
enum class MemoryProfile : uint8_t {
    EMBEDDED     = 0,  ///< < 64MB total RAM - minimal footprint
    IOT          = 1,  ///< 64-256MB RAM - constrained environment
    EDGE         = 2,  ///< 256MB-1GB RAM - edge computing
    STANDARD     = 3,  ///< 1-8GB RAM - typical deployment
    HIGH_PERF    = 4,  ///< 8GB+ RAM - maximum performance
    CUSTOM       = 5,  ///< User-defined configuration
    AUTO_DETECT  = 255 ///< Detect at runtime based on available memory
};

/**
 * @brief Memory configuration for all IPB components
 *
 * All sizes are configurable and can be set at compile-time via defines
 * or at runtime via the configuration API.
 */
struct MemoryConfig {
    // =========================================================================
    // Scheduler Configuration
    // =========================================================================

    /// Maximum tasks in EDF scheduler queue
    size_t scheduler_max_queue_size = 10000;

    /// Worker thread count (0 = auto-detect based on CPU cores)
    size_t scheduler_worker_threads = 0;

    // =========================================================================
    // Message Bus Configuration
    // =========================================================================

    /// Maximum number of channels
    size_t message_bus_max_channels = 64;

    /// Default buffer size per channel (must be power of 2)
    size_t message_bus_buffer_size = 4096;

    /// Number of dispatcher threads (0 = auto-detect)
    size_t message_bus_dispatcher_threads = 0;

    // =========================================================================
    // Memory Pool Configuration
    // =========================================================================

    /// Initial capacity for small object pool (<=64 bytes)
    size_t pool_small_capacity = 1024;

    /// Initial capacity for medium object pool (<=256 bytes)
    size_t pool_medium_capacity = 512;

    /// Initial capacity for large object pool (<=1024 bytes)
    size_t pool_large_capacity = 256;

    /// Block size for pool growth
    size_t pool_block_size = 64;

    // =========================================================================
    // Router Configuration
    // =========================================================================

    /// Maximum number of routing rules
    size_t router_max_rules = 256;

    /// Maximum sinks
    size_t router_max_sinks = 32;

    /// Batch size for routing
    size_t router_batch_size = 16;

    // =========================================================================
    // Pattern Matcher Cache
    // =========================================================================

    /// Maximum cached compiled patterns
    size_t pattern_cache_size = 128;

    // =========================================================================
    // Computed Values
    // =========================================================================

    /// Get estimated total memory footprint in bytes
    [[nodiscard]] constexpr size_t estimated_footprint() const noexcept {
        // Rough estimation based on structure sizes
        constexpr size_t TASK_SIZE = 256;        // ScheduledTask ~200 bytes + padding
        constexpr size_t MESSAGE_SIZE = 384;     // Message ~300 bytes + slot overhead
        constexpr size_t CHANNEL_OVERHEAD = 256; // Per-channel overhead

        size_t scheduler_mem = scheduler_max_queue_size * TASK_SIZE;
        size_t message_bus_mem = message_bus_max_channels *
                                 (message_bus_buffer_size * MESSAGE_SIZE + CHANNEL_OVERHEAD);
        size_t pool_mem = (pool_small_capacity * 64) +
                          (pool_medium_capacity * 256) +
                          (pool_large_capacity * 1024);

        return scheduler_mem + message_bus_mem + pool_mem;
    }

    /// Get estimated footprint in MB
    [[nodiscard]] constexpr size_t estimated_footprint_mb() const noexcept {
        return estimated_footprint() / (1024 * 1024);
    }

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Create configuration for embedded systems (<64MB RAM)
     *
     * Optimized for minimal memory footprint:
     * - Estimated footprint: ~5-10MB
     * - Suitable for microcontrollers and constrained devices
     */
    static constexpr MemoryConfig embedded() noexcept {
        return MemoryConfig{
            .scheduler_max_queue_size = 256,
            .scheduler_worker_threads = 1,
            .message_bus_max_channels = 8,
            .message_bus_buffer_size = 256,  // Power of 2
            .message_bus_dispatcher_threads = 1,
            .pool_small_capacity = 128,
            .pool_medium_capacity = 64,
            .pool_large_capacity = 32,
            .pool_block_size = 16,
            .router_max_rules = 32,
            .router_max_sinks = 8,
            .router_batch_size = 4,
            .pattern_cache_size = 16
        };
    }

    /**
     * @brief Create configuration for IoT devices (64-256MB RAM)
     *
     * Balanced for constrained environments:
     * - Estimated footprint: ~20-50MB
     * - Suitable for Raspberry Pi, industrial IoT gateways
     */
    static constexpr MemoryConfig iot() noexcept {
        return MemoryConfig{
            .scheduler_max_queue_size = 1000,
            .scheduler_worker_threads = 2,
            .message_bus_max_channels = 16,
            .message_bus_buffer_size = 1024,  // Power of 2
            .message_bus_dispatcher_threads = 2,
            .pool_small_capacity = 256,
            .pool_medium_capacity = 128,
            .pool_large_capacity = 64,
            .pool_block_size = 32,
            .router_max_rules = 64,
            .router_max_sinks = 16,
            .router_batch_size = 8,
            .pattern_cache_size = 32
        };
    }

    /**
     * @brief Create configuration for edge computing (256MB-1GB RAM)
     *
     * Good balance of performance and memory:
     * - Estimated footprint: ~50-150MB
     * - Suitable for edge servers, industrial PCs
     */
    static constexpr MemoryConfig edge() noexcept {
        return MemoryConfig{
            .scheduler_max_queue_size = 5000,
            .scheduler_worker_threads = 0,  // Auto-detect
            .message_bus_max_channels = 32,
            .message_bus_buffer_size = 2048,  // Power of 2
            .message_bus_dispatcher_threads = 0,  // Auto-detect
            .pool_small_capacity = 512,
            .pool_medium_capacity = 256,
            .pool_large_capacity = 128,
            .pool_block_size = 64,
            .router_max_rules = 128,
            .router_max_sinks = 24,
            .router_batch_size = 16,
            .pattern_cache_size = 64
        };
    }

    /**
     * @brief Create configuration for standard servers (1-8GB RAM)
     *
     * Default configuration for most deployments:
     * - Estimated footprint: ~100-400MB
     * - Good throughput with reasonable memory usage
     */
    static constexpr MemoryConfig standard() noexcept {
        return MemoryConfig{
            .scheduler_max_queue_size = 10000,
            .scheduler_worker_threads = 0,  // Auto-detect
            .message_bus_max_channels = 64,
            .message_bus_buffer_size = 4096,  // Power of 2
            .message_bus_dispatcher_threads = 0,  // Auto-detect
            .pool_small_capacity = 1024,
            .pool_medium_capacity = 512,
            .pool_large_capacity = 256,
            .pool_block_size = 64,
            .router_max_rules = 256,
            .router_max_sinks = 32,
            .router_batch_size = 16,
            .pattern_cache_size = 128
        };
    }

    /**
     * @brief Create configuration for high-performance servers (8GB+ RAM)
     *
     * Maximum throughput configuration:
     * - Estimated footprint: ~500MB-2GB
     * - Optimized for >5M messages/second
     */
    static constexpr MemoryConfig high_performance() noexcept {
        return MemoryConfig{
            .scheduler_max_queue_size = 50000,
            .scheduler_worker_threads = 0,  // Auto-detect
            .message_bus_max_channels = 256,
            .message_bus_buffer_size = 16384,  // Power of 2
            .message_bus_dispatcher_threads = 0,  // Auto-detect
            .pool_small_capacity = 4096,
            .pool_medium_capacity = 2048,
            .pool_large_capacity = 1024,
            .pool_block_size = 128,
            .router_max_rules = 1024,
            .router_max_sinks = 128,
            .router_batch_size = 64,
            .pattern_cache_size = 512
        };
    }

    /**
     * @brief Create configuration based on memory profile enum
     */
    static constexpr MemoryConfig from_profile(MemoryProfile profile) noexcept {
        switch (profile) {
            case MemoryProfile::EMBEDDED:
                return embedded();
            case MemoryProfile::IOT:
                return iot();
            case MemoryProfile::EDGE:
                return edge();
            case MemoryProfile::STANDARD:
                return standard();
            case MemoryProfile::HIGH_PERF:
                return high_performance();
            default:
                return standard();
        }
    }

    /**
     * @brief Auto-detect appropriate profile based on available memory
     * @param available_memory_bytes Available system memory in bytes
     */
    static MemoryConfig create_for_memory(uint64_t available_memory_bytes) noexcept {
        constexpr uint64_t MB = 1024ULL * 1024ULL;
        constexpr uint64_t GB = 1024ULL * MB;

        if (available_memory_bytes < 64 * MB) {
            return embedded();
        } else if (available_memory_bytes < 256 * MB) {
            return iot();
        } else if (available_memory_bytes < 1 * GB) {
            return edge();
        } else if (available_memory_bytes < 8 * GB) {
            return standard();
        } else {
            return high_performance();
        }
    }

    /**
     * @brief Auto-detect using runtime memory detection
     */
    static MemoryConfig auto_detect() noexcept {
        uint64_t available = platform::get_available_memory();
        return create_for_memory(available);
    }

    /**
     * @brief Scale configuration based on target memory limit
     * @param target_memory_mb Maximum memory to use in MB
     */
    MemoryConfig scaled_to(size_t target_memory_mb) const noexcept {
        if (target_memory_mb == 0) {
            return *this;
        }

        size_t current_mb = estimated_footprint_mb();
        if (current_mb <= target_memory_mb) {
            return *this;  // Already under limit
        }

        // Calculate scale factor
        double scale = static_cast<double>(target_memory_mb) / static_cast<double>(current_mb);

        MemoryConfig scaled = *this;

        // Scale down all size parameters
        auto scale_size = [scale](size_t value, size_t minimum) -> size_t {
            size_t scaled_value = static_cast<size_t>(value * scale);
            return std::max(scaled_value, minimum);
        };

        // Scale with minimum values to ensure functionality
        scaled.scheduler_max_queue_size = scale_size(scheduler_max_queue_size, 100);
        scaled.message_bus_max_channels = scale_size(message_bus_max_channels, 4);

        // Buffer size must be power of 2
        size_t new_buffer = scale_size(message_bus_buffer_size, 256);
        scaled.message_bus_buffer_size = next_power_of_2(new_buffer);

        scaled.pool_small_capacity = scale_size(pool_small_capacity, 32);
        scaled.pool_medium_capacity = scale_size(pool_medium_capacity, 16);
        scaled.pool_large_capacity = scale_size(pool_large_capacity, 8);
        scaled.router_max_rules = scale_size(router_max_rules, 16);
        scaled.router_max_sinks = scale_size(router_max_sinks, 4);
        scaled.pattern_cache_size = scale_size(pattern_cache_size, 8);

        return scaled;
    }

    /**
     * @brief Validate configuration
     * @return true if all values are within acceptable ranges
     */
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        // Check buffer size is power of 2
        if ((message_bus_buffer_size & (message_bus_buffer_size - 1)) != 0) {
            return false;
        }

        // Check minimum values
        if (scheduler_max_queue_size < 10) return false;
        if (message_bus_max_channels < 1) return false;
        if (message_bus_buffer_size < 64) return false;

        return true;
    }

private:
    static constexpr size_t next_power_of_2(size_t n) noexcept {
        if (n == 0) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }
};

// ============================================================================
// COMPILE-TIME PROFILE SELECTION
// ============================================================================

#if defined(IPB_MEMORY_PROFILE_EMBEDDED)
inline constexpr MemoryProfile DEFAULT_MEMORY_PROFILE = MemoryProfile::EMBEDDED;
#elif defined(IPB_MEMORY_PROFILE_IOT)
inline constexpr MemoryProfile DEFAULT_MEMORY_PROFILE = MemoryProfile::IOT;
#elif defined(IPB_MEMORY_PROFILE_EDGE)
inline constexpr MemoryProfile DEFAULT_MEMORY_PROFILE = MemoryProfile::EDGE;
#elif defined(IPB_MEMORY_PROFILE_HIGH_PERF)
inline constexpr MemoryProfile DEFAULT_MEMORY_PROFILE = MemoryProfile::HIGH_PERF;
#elif defined(IPB_MEMORY_PROFILE_AUTO)
inline constexpr MemoryProfile DEFAULT_MEMORY_PROFILE = MemoryProfile::AUTO_DETECT;
#else
// Default to STANDARD profile
inline constexpr MemoryProfile DEFAULT_MEMORY_PROFILE = MemoryProfile::STANDARD;
#endif

/**
 * @brief Get the default memory configuration based on compile-time profile
 */
inline MemoryConfig get_default_memory_config() noexcept {
    if constexpr (DEFAULT_MEMORY_PROFILE == MemoryProfile::AUTO_DETECT) {
        return MemoryConfig::auto_detect();
    } else {
        return MemoryConfig::from_profile(DEFAULT_MEMORY_PROFILE);
    }
}

/**
 * @brief Global memory configuration instance
 *
 * Can be modified before IPB initialization to customize memory usage.
 * Thread-safe for reads after initialization.
 */
class GlobalMemoryConfig {
public:
    static MemoryConfig& instance() noexcept {
        static MemoryConfig config = get_default_memory_config();
        return config;
    }

    static void set(const MemoryConfig& config) noexcept {
        instance() = config;
    }

    static void set_profile(MemoryProfile profile) noexcept {
        if (profile == MemoryProfile::AUTO_DETECT) {
            instance() = MemoryConfig::auto_detect();
        } else {
            instance() = MemoryConfig::from_profile(profile);
        }
    }

    static void set_memory_limit(size_t max_memory_mb) noexcept {
        instance() = instance().scaled_to(max_memory_mb);
    }

    // Prevent instantiation
    GlobalMemoryConfig() = delete;
};

}  // namespace ipb::common
