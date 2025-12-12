#pragma once

/**
 * @file backpressure.hpp
 * @brief Enterprise-grade backpressure handling for flow control
 *
 * Comprehensive backpressure management features:
 * - Multi-level pressure detection (queue depth, latency, memory)
 * - Adaptive throttling with configurable strategies
 * - Producer/consumer flow control coordination
 * - Graceful degradation under load
 * - Pressure propagation across pipeline stages
 * - Metrics and alerting integration
 *
 * Backpressure strategies:
 * - DROP_OLDEST: Drop oldest items when full (lossy)
 * - DROP_NEWEST: Reject new items when full (lossy)
 * - BLOCK: Block producers until space available (lossless)
 * - SAMPLE: Keep every Nth item (lossy, uniform)
 * - THROTTLE: Slow down producer rate (lossless)
 */

#include <ipb/common/platform.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ipb::common {

/**
 * @brief Backpressure strategy enumeration
 */
enum class BackpressureStrategy {
    DROP_OLDEST,    // Drop oldest items when queue is full
    DROP_NEWEST,    // Reject new items when queue is full
    BLOCK,          // Block producer until space available
    SAMPLE,         // Keep every Nth item
    THROTTLE        // Dynamically slow down producer
};

/**
 * @brief Pressure level indicators
 */
enum class PressureLevel : uint8_t {
    NONE = 0,       // No pressure - operating normally
    LOW = 1,        // Low pressure - minor congestion
    MEDIUM = 2,     // Medium pressure - noticeable delays
    HIGH = 3,       // High pressure - approaching limits
    CRITICAL = 4    // Critical pressure - system overloaded
};

/**
 * @brief Backpressure configuration
 */
struct BackpressureConfig {
    BackpressureStrategy strategy{BackpressureStrategy::THROTTLE};

    // Queue-based thresholds (as percentage of capacity)
    double low_watermark{0.5};      // 50% - start mild throttling
    double high_watermark{0.8};     // 80% - aggressive throttling
    double critical_watermark{0.95}; // 95% - maximum throttling/dropping

    // Latency-based thresholds (nanoseconds)
    int64_t target_latency_ns{1000000};      // 1ms target
    int64_t max_latency_ns{10000000};        // 10ms max before critical

    // Memory-based thresholds (bytes)
    size_t target_memory_bytes{0};           // 0 = disabled
    size_t max_memory_bytes{0};              // 0 = disabled

    // Sampling rate for SAMPLE strategy
    size_t sample_rate{10};  // Keep 1 in N items

    // Throttle parameters
    double min_throughput_factor{0.1};  // Minimum 10% of normal rate
    int64_t throttle_step_ns{100000};   // 100μs throttle increments
    int64_t max_throttle_ns{100000000}; // 100ms max throttle delay

    // Recovery parameters
    double recovery_factor{0.9};  // Drop to 90% of threshold to recover
    int64_t hysteresis_ns{1000000000};  // 1s hysteresis to avoid oscillation
};

/**
 * @brief Backpressure statistics
 */
struct BackpressureStats {
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> items_received{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> items_processed{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> items_dropped{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> items_sampled_out{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> throttle_events{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> block_events{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> total_throttle_ns{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> total_block_ns{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> pressure_changes{0};

    double drop_rate() const noexcept {
        auto total = items_received.load(std::memory_order_relaxed);
        auto dropped = items_dropped.load(std::memory_order_relaxed);
        return total > 0 ? static_cast<double>(dropped) / total * 100.0 : 0.0;
    }

    double throughput_factor() const noexcept {
        auto received = items_received.load(std::memory_order_relaxed);
        auto processed = items_processed.load(std::memory_order_relaxed);
        return received > 0 ? static_cast<double>(processed) / received : 1.0;
    }

    void reset() noexcept {
        items_received.store(0, std::memory_order_relaxed);
        items_processed.store(0, std::memory_order_relaxed);
        items_dropped.store(0, std::memory_order_relaxed);
        items_sampled_out.store(0, std::memory_order_relaxed);
        throttle_events.store(0, std::memory_order_relaxed);
        block_events.store(0, std::memory_order_relaxed);
        total_throttle_ns.store(0, std::memory_order_relaxed);
        total_block_ns.store(0, std::memory_order_relaxed);
        pressure_changes.store(0, std::memory_order_relaxed);
    }
};

/**
 * @brief Pressure sensor for monitoring system load
 *
 * Aggregates multiple signals into a single pressure level.
 */
class alignas(IPB_CACHE_LINE_SIZE) PressureSensor {
public:
    explicit PressureSensor(const BackpressureConfig& config = {})
        : config_(config)
    {}

    /**
     * @brief Update queue fill level
     * @param current Current queue size
     * @param capacity Maximum queue capacity
     */
    void update_queue_fill(size_t current, size_t capacity) noexcept {
        if (capacity == 0) return;
        double fill = static_cast<double>(current) / capacity;
        queue_fill_.store(fill, std::memory_order_relaxed);
    }

    /**
     * @brief Update processing latency
     * @param latency_ns Observed latency in nanoseconds
     */
    void update_latency(int64_t latency_ns) noexcept {
        // Exponential moving average
        int64_t current = latency_ema_ns_.load(std::memory_order_relaxed);
        int64_t updated = static_cast<int64_t>(0.1 * latency_ns + 0.9 * current);
        latency_ema_ns_.store(updated, std::memory_order_relaxed);
    }

    /**
     * @brief Update memory usage
     * @param bytes Current memory usage
     */
    void update_memory(size_t bytes) noexcept {
        memory_bytes_.store(bytes, std::memory_order_relaxed);
    }

    /**
     * @brief Get current pressure level
     */
    PressureLevel level() const noexcept {
        double fill = queue_fill_.load(std::memory_order_relaxed);
        int64_t latency = latency_ema_ns_.load(std::memory_order_relaxed);
        size_t memory = memory_bytes_.load(std::memory_order_relaxed);

        // Queue-based pressure
        PressureLevel queue_pressure = PressureLevel::NONE;
        if (fill >= config_.critical_watermark) {
            queue_pressure = PressureLevel::CRITICAL;
        } else if (fill >= config_.high_watermark) {
            queue_pressure = PressureLevel::HIGH;
        } else if (fill >= config_.low_watermark) {
            queue_pressure = PressureLevel::MEDIUM;
        } else if (fill > 0.25) {
            queue_pressure = PressureLevel::LOW;
        }

        // Latency-based pressure
        PressureLevel latency_pressure = PressureLevel::NONE;
        if (latency >= config_.max_latency_ns) {
            latency_pressure = PressureLevel::CRITICAL;
        } else if (latency >= config_.max_latency_ns * 3 / 4) {
            latency_pressure = PressureLevel::HIGH;
        } else if (latency >= config_.target_latency_ns * 2) {
            latency_pressure = PressureLevel::MEDIUM;
        } else if (latency >= config_.target_latency_ns) {
            latency_pressure = PressureLevel::LOW;
        }

        // Memory-based pressure
        PressureLevel memory_pressure = PressureLevel::NONE;
        if (config_.max_memory_bytes > 0) {
            double mem_ratio = static_cast<double>(memory) / config_.max_memory_bytes;
            if (mem_ratio >= 0.95) {
                memory_pressure = PressureLevel::CRITICAL;
            } else if (mem_ratio >= 0.80) {
                memory_pressure = PressureLevel::HIGH;
            } else if (mem_ratio >= 0.60) {
                memory_pressure = PressureLevel::MEDIUM;
            } else if (mem_ratio >= 0.40) {
                memory_pressure = PressureLevel::LOW;
            }
        }

        // Return maximum pressure across all signals
        auto max_val = static_cast<uint8_t>(queue_pressure);
        max_val = std::max(max_val, static_cast<uint8_t>(latency_pressure));
        max_val = std::max(max_val, static_cast<uint8_t>(memory_pressure));
        return static_cast<PressureLevel>(max_val);
    }

    /**
     * @brief Get numeric pressure value (0.0 - 1.0)
     */
    double pressure_value() const noexcept {
        auto lvl = level();
        switch (lvl) {
            case PressureLevel::NONE: return 0.0;
            case PressureLevel::LOW: return 0.25;
            case PressureLevel::MEDIUM: return 0.5;
            case PressureLevel::HIGH: return 0.75;
            case PressureLevel::CRITICAL: return 1.0;
        }
        return 0.0;
    }

    const BackpressureConfig& config() const noexcept { return config_; }

private:
    BackpressureConfig config_;
    std::atomic<double> queue_fill_{0.0};
    std::atomic<int64_t> latency_ema_ns_{0};
    std::atomic<size_t> memory_bytes_{0};
};

/**
 * @brief Backpressure controller for flow regulation
 *
 * Implements the configured backpressure strategy and provides
 * throttling/dropping decisions.
 */
class BackpressureController {
public:
    using DropCallback = std::function<void(size_t dropped_count)>;
    using PressureCallback = std::function<void(PressureLevel level)>;

    explicit BackpressureController(const BackpressureConfig& config = {})
        : config_(config)
        , sensor_(config)
        , current_level_(PressureLevel::NONE)
        , sample_counter_(0)
        , throttle_ns_(0)
    {}

    /**
     * @brief Check if item should be accepted
     *
     * Call this before accepting a new item into the pipeline.
     * May block if strategy is BLOCK and system is under pressure.
     *
     * @return true if item should be processed, false if dropped
     */
    bool should_accept() noexcept {
        stats_.items_received.fetch_add(1, std::memory_order_relaxed);

        // Update pressure level
        PressureLevel new_level = sensor_.level();
        update_pressure_level(new_level);

        switch (config_.strategy) {
            case BackpressureStrategy::DROP_OLDEST:
                // Always accept new items (caller must handle dropping oldest)
                return true;

            case BackpressureStrategy::DROP_NEWEST:
                return handle_drop_newest(new_level);

            case BackpressureStrategy::BLOCK:
                return handle_block(new_level);

            case BackpressureStrategy::SAMPLE:
                return handle_sample(new_level);

            case BackpressureStrategy::THROTTLE:
                return handle_throttle(new_level);
        }

        return true;
    }

    /**
     * @brief Mark item as processed
     */
    void item_processed() noexcept {
        stats_.items_processed.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Mark item as dropped
     */
    void item_dropped() noexcept {
        stats_.items_dropped.fetch_add(1, std::memory_order_relaxed);

        if (drop_callback_) {
            drop_callback_(1);
        }
    }

    /**
     * @brief Update sensor with queue metrics
     */
    void update_queue(size_t current, size_t capacity) noexcept {
        sensor_.update_queue_fill(current, capacity);
    }

    /**
     * @brief Update sensor with latency metric
     */
    void update_latency(int64_t latency_ns) noexcept {
        sensor_.update_latency(latency_ns);
    }

    /**
     * @brief Update sensor with memory metric
     */
    void update_memory(size_t bytes) noexcept {
        sensor_.update_memory(bytes);
    }

    /**
     * @brief Set callback for dropped items
     */
    void set_drop_callback(DropCallback callback) {
        drop_callback_ = std::move(callback);
    }

    /**
     * @brief Set callback for pressure level changes
     */
    void set_pressure_callback(PressureCallback callback) {
        pressure_callback_ = std::move(callback);
    }

    /**
     * @brief Get current pressure level
     */
    PressureLevel pressure_level() const noexcept {
        return current_level_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get current throttle delay
     */
    int64_t throttle_delay_ns() const noexcept {
        return throttle_ns_.load(std::memory_order_relaxed);
    }

    const BackpressureConfig& config() const noexcept { return config_; }
    const BackpressureStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

private:
    BackpressureConfig config_;
    PressureSensor sensor_;
    mutable BackpressureStats stats_;

    std::atomic<PressureLevel> current_level_;
    std::atomic<uint64_t> sample_counter_;
    std::atomic<int64_t> throttle_ns_;
    std::atomic<int64_t> last_level_change_ns_{0};

    DropCallback drop_callback_;
    PressureCallback pressure_callback_;

    void update_pressure_level(PressureLevel new_level) noexcept {
        PressureLevel old_level = current_level_.load(std::memory_order_relaxed);

        if (new_level == old_level) {
            return;
        }

        // Apply hysteresis
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();

        int64_t last_change = last_level_change_ns_.load(std::memory_order_relaxed);
        if (now_ns - last_change < config_.hysteresis_ns) {
            // Only allow level increases during hysteresis
            if (static_cast<uint8_t>(new_level) <= static_cast<uint8_t>(old_level)) {
                return;
            }
        }

        if (current_level_.compare_exchange_strong(old_level, new_level)) {
            last_level_change_ns_.store(now_ns, std::memory_order_relaxed);
            stats_.pressure_changes.fetch_add(1, std::memory_order_relaxed);

            if (pressure_callback_) {
                pressure_callback_(new_level);
            }
        }
    }

    bool handle_drop_newest(PressureLevel level) noexcept {
        if (level >= PressureLevel::CRITICAL) {
            stats_.items_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    bool handle_block(PressureLevel level) noexcept {
        if (level < PressureLevel::HIGH) {
            return true;
        }

        stats_.block_events.fetch_add(1, std::memory_order_relaxed);

        auto start = std::chrono::steady_clock::now();
        int64_t max_block_ns = config_.max_throttle_ns;

        // Spin/sleep until pressure reduces
        while (sensor_.level() >= PressureLevel::HIGH) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                elapsed
            ).count();

            if (elapsed_ns >= max_block_ns) {
                // Timeout - drop item
                stats_.items_dropped.fetch_add(1, std::memory_order_relaxed);
                stats_.total_block_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
                return false;
            }

            // Short sleep
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        stats_.total_block_ns.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
            std::memory_order_relaxed
        );

        return true;
    }

    bool handle_sample(PressureLevel level) noexcept {
        if (level < PressureLevel::MEDIUM) {
            return true;  // No sampling when not under pressure
        }

        // Calculate dynamic sample rate based on pressure
        size_t rate = config_.sample_rate;
        if (level >= PressureLevel::CRITICAL) {
            rate *= 4;  // Drop 75% at critical
        } else if (level >= PressureLevel::HIGH) {
            rate *= 2;  // Drop 50% at high
        }

        uint64_t count = sample_counter_.fetch_add(1, std::memory_order_relaxed);
        if (count % rate != 0) {
            stats_.items_sampled_out.fetch_add(1, std::memory_order_relaxed);
            stats_.items_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        return true;
    }

    bool handle_throttle(PressureLevel level) noexcept {
        // Calculate throttle delay based on pressure
        int64_t delay_ns = 0;
        switch (level) {
            case PressureLevel::NONE:
                delay_ns = 0;
                break;
            case PressureLevel::LOW:
                delay_ns = config_.throttle_step_ns;
                break;
            case PressureLevel::MEDIUM:
                delay_ns = config_.throttle_step_ns * 4;
                break;
            case PressureLevel::HIGH:
                delay_ns = config_.throttle_step_ns * 16;
                break;
            case PressureLevel::CRITICAL:
                delay_ns = config_.max_throttle_ns;
                break;
        }

        delay_ns = std::min(delay_ns, config_.max_throttle_ns);
        throttle_ns_.store(delay_ns, std::memory_order_relaxed);

        if (delay_ns > 0) {
            stats_.throttle_events.fetch_add(1, std::memory_order_relaxed);
            stats_.total_throttle_ns.fetch_add(delay_ns, std::memory_order_relaxed);

            if (delay_ns < 10000) {
                // Spin for very short delays
                auto deadline = std::chrono::steady_clock::now() +
                               std::chrono::nanoseconds(delay_ns);
                while (std::chrono::steady_clock::now() < deadline) {
                    IPB_CPU_PAUSE();
                }
            } else {
                std::this_thread::sleep_for(std::chrono::nanoseconds(delay_ns));
            }
        }

        return true;
    }
};

/**
 * @brief Pipeline stage with integrated backpressure
 *
 * Wraps a processing stage with automatic backpressure handling.
 */
template<typename Input, typename Output>
class BackpressureStage {
public:
    using Processor = std::function<std::optional<Output>(const Input&)>;

    BackpressureStage(const BackpressureConfig& config, Processor processor)
        : controller_(config)
        , processor_(std::move(processor))
    {}

    /**
     * @brief Process input with backpressure control
     * @return Output if processed, nullopt if dropped
     */
    std::optional<Output> process(const Input& input) {
        if (!controller_.should_accept()) {
            controller_.item_dropped();
            return std::nullopt;
        }

        auto start = std::chrono::steady_clock::now();

        auto result = processor_(input);

        auto elapsed = std::chrono::steady_clock::now() - start;
        controller_.update_latency(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
        );

        if (result) {
            controller_.item_processed();
        } else {
            controller_.item_dropped();
        }

        return result;
    }

    /**
     * @brief Update queue metrics from external source
     */
    void update_queue(size_t current, size_t capacity) {
        controller_.update_queue(current, capacity);
    }

    BackpressureController& controller() { return controller_; }
    const BackpressureController& controller() const { return controller_; }

private:
    BackpressureController controller_;
    Processor processor_;
};

/**
 * @brief Pressure propagation for multi-stage pipelines
 *
 * Propagates backpressure signals between connected stages.
 */
class PressurePropagator {
public:
    /**
     * @brief Add controller to propagation chain
     */
    void add_stage(BackpressureController* controller) {
        std::lock_guard lock(mutex_);
        stages_.push_back(controller);
    }

    /**
     * @brief Get maximum pressure across all stages
     */
    PressureLevel max_pressure() const {
        std::lock_guard lock(mutex_);
        PressureLevel max_level = PressureLevel::NONE;
        for (const auto* stage : stages_) {
            auto level = stage->pressure_level();
            if (static_cast<uint8_t>(level) > static_cast<uint8_t>(max_level)) {
                max_level = level;
            }
        }
        return max_level;
    }

    /**
     * @brief Check if any stage is under critical pressure
     */
    bool is_critical() const {
        return max_pressure() >= PressureLevel::CRITICAL;
    }

    /**
     * @brief Get aggregated statistics
     */
    void aggregate_stats(BackpressureStats& total) const {
        std::lock_guard lock(mutex_);

        for (const auto* stage : stages_) {
            const auto& s = stage->stats();
            total.items_received.fetch_add(
                s.items_received.load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
            total.items_processed.fetch_add(
                s.items_processed.load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
            total.items_dropped.fetch_add(
                s.items_dropped.load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
            total.throttle_events.fetch_add(
                s.throttle_events.load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
        }
    }

private:
    mutable std::mutex mutex_;
    std::vector<BackpressureController*> stages_;
};

} // namespace ipb::common
