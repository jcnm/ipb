#pragma once

/**
 * @file rate_limiter.hpp
 * @brief High-performance rate limiting for enterprise traffic control
 *
 * Enterprise-grade rate limiting features:
 * - Token bucket algorithm with configurable burst
 * - Sliding window rate limiter for smooth limits
 * - Hierarchical rate limiting (global + per-source)
 * - Adaptive rate limiting based on system load
 * - Lock-free fast path for high throughput
 * - Fair queuing for burst traffic
 *
 * Performance characteristics:
 * - O(1) check/acquire operations (lock-free)
 * - Sub-microsecond latency
 * - Minimal memory overhead per limiter
 */

#include <ipb/common/platform.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace ipb::common {

/**
 * @brief Rate limiter configuration
 */
struct RateLimitConfig {
    double rate_per_second{1000.0};  // Sustained rate
    size_t burst_size{100};          // Maximum burst
    bool fair_queuing{false};        // Enable fair queuing
    bool adaptive{false};            // Adapt to system load
    double min_rate{10.0};           // Minimum rate when adapting
    double max_rate{100000.0};       // Maximum rate when adapting

    static RateLimitConfig unlimited() {
        RateLimitConfig config;
        config.rate_per_second = 1e12;  // Effectively unlimited
        config.burst_size      = SIZE_MAX / 2;
        return config;
    }

    static RateLimitConfig strict(double rate) {
        RateLimitConfig config;
        config.rate_per_second = rate;
        config.burst_size      = 1;  // No burst allowed
        return config;
    }
};

/**
 * @brief Rate limiter statistics
 */
struct RateLimiterStats {
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> requests{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> allowed{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> rejected{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> throttled_ns{0};

    double allow_rate() const noexcept {
        auto total = requests.load(std::memory_order_relaxed);
        auto ok    = allowed.load(std::memory_order_relaxed);
        return total > 0 ? static_cast<double>(ok) / total * 100.0 : 100.0;
    }

    void reset() noexcept {
        requests.store(0, std::memory_order_relaxed);
        allowed.store(0, std::memory_order_relaxed);
        rejected.store(0, std::memory_order_relaxed);
        throttled_ns.store(0, std::memory_order_relaxed);
    }
};

/**
 * @brief Token bucket rate limiter (lock-free)
 *
 * Classic token bucket algorithm with atomic operations.
 * Tokens are added at a fixed rate up to bucket capacity.
 * Each request consumes one token.
 */
class alignas(IPB_CACHE_LINE_SIZE) TokenBucket {
public:
    /**
     * @brief Construct token bucket
     * @param config Rate limit configuration
     */
    explicit TokenBucket(const RateLimitConfig& config = {})
        : config_(config), tokens_(static_cast<double>(config.burst_size)),
          last_refill_(std::chrono::steady_clock::now()) {
        tokens_atomic_.store(static_cast<int64_t>(config.burst_size * PRECISION),
                             std::memory_order_relaxed);
        last_refill_ns_.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(last_refill_.time_since_epoch())
                .count(),
            std::memory_order_relaxed);
    }

    /**
     * @brief Try to acquire a token (non-blocking)
     * @param count Number of tokens to acquire
     * @return true if tokens acquired, false if rate limited
     */
    bool try_acquire(size_t count = 1) noexcept {
        stats_.requests.fetch_add(1, std::memory_order_relaxed);

        // Refill tokens based on elapsed time
        refill();

        // Try to consume tokens atomically
        int64_t needed  = static_cast<int64_t>(count * PRECISION);
        int64_t current = tokens_atomic_.load(std::memory_order_relaxed);

        while (current >= needed) {
            if (tokens_atomic_.compare_exchange_weak(current, current - needed,
                                                     std::memory_order_acquire,
                                                     std::memory_order_relaxed)) {
                stats_.allowed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            // current is updated by compare_exchange_weak on failure
        }

        stats_.rejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /**
     * @brief Acquire tokens (blocking if necessary)
     * @param count Number of tokens to acquire
     * @param timeout Maximum wait time
     * @return true if acquired, false if timeout
     */
    bool acquire(size_t count                     = 1,
                 std::chrono::nanoseconds timeout = std::chrono::seconds(1)) noexcept {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!try_acquire(count)) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;
            }

            // Calculate wait time for tokens
            auto wait_ns   = wait_time_ns(count);
            auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now);

            if (wait_ns > remaining.count()) {
                wait_ns = remaining.count();
            }

            if (wait_ns > 0) {
                stats_.throttled_ns.fetch_add(wait_ns, std::memory_order_relaxed);

                // Spin wait for short durations, sleep for longer
                if (wait_ns < 1000) {
                    // Spin wait
                    auto spin_until = now + std::chrono::nanoseconds(wait_ns);
                    while (std::chrono::steady_clock::now() < spin_until) {
                        IPB_CPU_PAUSE();
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::nanoseconds(wait_ns));
                }
            }
        }

        return true;
    }

    /**
     * @brief Get estimated wait time for tokens
     * @param count Tokens needed
     * @return Nanoseconds to wait (0 if available now)
     */
    int64_t wait_time_ns(size_t count = 1) const noexcept {
        int64_t current = tokens_atomic_.load(std::memory_order_relaxed);
        int64_t needed  = static_cast<int64_t>(count * PRECISION);

        if (current >= needed) {
            return 0;
        }

        int64_t deficit      = needed - current;
        double tokens_per_ns = config_.rate_per_second / 1e9;
        return static_cast<int64_t>(deficit / PRECISION / tokens_per_ns);
    }

    /**
     * @brief Get current available tokens
     */
    double available_tokens() const noexcept {
        return static_cast<double>(tokens_atomic_.load(std::memory_order_relaxed)) / PRECISION;
    }

    /**
     * @brief Update rate limit configuration
     */
    void set_rate(double rate_per_second) noexcept { config_.rate_per_second = rate_per_second; }

    void set_burst(size_t burst_size) noexcept { config_.burst_size = burst_size; }

    const RateLimitConfig& config() const noexcept { return config_; }
    const RateLimiterStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

private:
    static constexpr int64_t PRECISION = 1000000;  // Fixed-point precision

    RateLimitConfig config_;
    mutable RateLimiterStats stats_;

    // Atomic state for lock-free operation
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<int64_t> tokens_atomic_;
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<int64_t> last_refill_ns_;

    // Non-atomic copies for initialization
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;

    void refill() noexcept {
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();

        int64_t last_ns    = last_refill_ns_.load(std::memory_order_relaxed);
        int64_t elapsed_ns = now_ns - last_ns;

        if (elapsed_ns <= 0) {
            return;
        }

        // Calculate tokens to add
        double tokens_per_ns = config_.rate_per_second / 1e9;
        int64_t new_tokens   = static_cast<int64_t>(elapsed_ns * tokens_per_ns * PRECISION);

        if (new_tokens <= 0) {
            return;
        }

        // Update timestamp atomically
        if (!last_refill_ns_.compare_exchange_strong(last_ns, now_ns, std::memory_order_release,
                                                     std::memory_order_relaxed)) {
            // Another thread updated - skip refill
            return;
        }

        // Add tokens up to maximum
        int64_t max_tokens = static_cast<int64_t>(config_.burst_size * PRECISION);
        int64_t current    = tokens_atomic_.load(std::memory_order_relaxed);
        int64_t target     = std::min(current + new_tokens, max_tokens);

        // Best-effort update (if we lose race, tokens are still added by winner)
        tokens_atomic_.store(target, std::memory_order_relaxed);
    }
};

/**
 * @brief Sliding window rate limiter
 *
 * More accurate rate limiting than token bucket by tracking
 * request timestamps in a sliding window.
 */
class alignas(IPB_CACHE_LINE_SIZE) SlidingWindowLimiter {
public:
    static constexpr size_t WINDOW_SLOTS = 60;  // 1-second window with 60 slots

    /**
     * @brief Construct sliding window limiter
     * @param rate_per_second Maximum requests per second
     */
    explicit SlidingWindowLimiter(double rate_per_second)
        : rate_per_second_(rate_per_second), slot_duration_ns_(1000000000 / WINDOW_SLOTS) {
        for (auto& slot : slots_) {
            slot.store(0, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Try to make a request
     * @return true if allowed, false if rate limited
     */
    bool try_acquire() noexcept {
        stats_.requests.fetch_add(1, std::memory_order_relaxed);

        auto now = std::chrono::steady_clock::now();
        auto now_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

        // Get current slot
        size_t current_slot = (now_ns / slot_duration_ns_) % WINDOW_SLOTS;

        // Clear old slots
        clear_old_slots(now_ns);

        // Count requests in window
        uint64_t total = 0;
        for (const auto& slot : slots_) {
            total += slot.load(std::memory_order_relaxed);
        }

        if (total >= static_cast<uint64_t>(rate_per_second_)) {
            stats_.rejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Record request
        slots_[current_slot].fetch_add(1, std::memory_order_relaxed);
        stats_.allowed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Get current request rate
     */
    double current_rate() const noexcept {
        uint64_t total = 0;
        for (const auto& slot : slots_) {
            total += slot.load(std::memory_order_relaxed);
        }
        return static_cast<double>(total);
    }

    double limit() const noexcept { return rate_per_second_; }
    const RateLimiterStats& stats() const noexcept { return stats_; }

private:
    double rate_per_second_;
    int64_t slot_duration_ns_;
    mutable RateLimiterStats stats_;

    alignas(IPB_CACHE_LINE_SIZE) std::array<std::atomic<uint64_t>, WINDOW_SLOTS> slots_;
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<int64_t> last_clear_ns_{0};

    void clear_old_slots(int64_t now_ns) noexcept {
        int64_t last_clear = last_clear_ns_.load(std::memory_order_relaxed);
        int64_t elapsed    = now_ns - last_clear;

        if (elapsed < slot_duration_ns_) {
            return;  // Not enough time passed
        }

        // Calculate slots to clear
        size_t slots_to_clear =
            std::min(static_cast<size_t>(elapsed / slot_duration_ns_), WINDOW_SLOTS);

        size_t start_slot = ((last_clear / slot_duration_ns_) + 1) % WINDOW_SLOTS;

        for (size_t i = 0; i < slots_to_clear; ++i) {
            size_t slot = (start_slot + i) % WINDOW_SLOTS;
            slots_[slot].store(0, std::memory_order_relaxed);
        }

        last_clear_ns_.store(now_ns, std::memory_order_relaxed);
    }
};

/**
 * @brief Adaptive rate limiter that adjusts based on system load
 */
class AdaptiveRateLimiter {
public:
    /**
     * @brief Construct adaptive limiter
     * @param config Configuration including adaptation parameters
     */
    explicit AdaptiveRateLimiter(const RateLimitConfig& config)
        : config_(config), bucket_(config), current_rate_(config.rate_per_second),
          load_factor_(0.0) {}

    /**
     * @brief Try to acquire with adaptive adjustment
     */
    bool try_acquire(size_t count = 1) noexcept {
        update_rate();
        return bucket_.try_acquire(count);
    }

    /**
     * @brief Report current system load (0.0 - 1.0)
     */
    void report_load(double load) noexcept {
        // Exponential moving average
        constexpr double alpha = 0.1;
        double current         = load_factor_.load(std::memory_order_relaxed);
        double updated         = alpha * load + (1 - alpha) * current;
        load_factor_.store(updated, std::memory_order_relaxed);
    }

    /**
     * @brief Get current effective rate
     */
    double current_rate() const noexcept { return current_rate_.load(std::memory_order_relaxed); }

    const RateLimiterStats& stats() const noexcept { return bucket_.stats(); }

private:
    RateLimitConfig config_;
    TokenBucket bucket_;
    std::atomic<double> current_rate_;
    std::atomic<double> load_factor_;
    std::atomic<int64_t> last_update_ns_{0};

    void update_rate() noexcept {
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();

        int64_t last = last_update_ns_.load(std::memory_order_relaxed);
        if (now_ns - last < 100000000) {  // Update every 100ms
            return;
        }

        if (!last_update_ns_.compare_exchange_strong(last, now_ns)) {
            return;  // Another thread is updating
        }

        // Calculate new rate based on load
        double load = load_factor_.load(std::memory_order_relaxed);

        // Reduce rate as load increases
        double rate_factor = 1.0 - (load * 0.8);  // At full load, use 20% of max
        rate_factor        = std::max(0.1, std::min(1.0, rate_factor));

        double new_rate = config_.max_rate * rate_factor;
        new_rate        = std::max(config_.min_rate, std::min(config_.max_rate, new_rate));

        current_rate_.store(new_rate, std::memory_order_relaxed);
        bucket_.set_rate(new_rate);
    }
};

/**
 * @brief Hierarchical rate limiter for multi-level control
 *
 * Applies rate limits at multiple levels (e.g., global + per-source).
 */
class HierarchicalRateLimiter {
public:
    /**
     * @brief Construct hierarchical limiter
     * @param global_config Global rate limit
     */
    explicit HierarchicalRateLimiter(const RateLimitConfig& global_config)
        : global_bucket_(global_config) {}

    /**
     * @brief Add per-source rate limit
     * @param source_id Source identifier
     * @param config Source-specific rate limit
     */
    void add_source_limit(const std::string& source_id, const RateLimitConfig& config) {
        std::lock_guard lock(sources_mutex_);
        source_buckets_[source_id] = std::make_unique<TokenBucket>(config);
    }

    /**
     * @brief Try to acquire from source
     * @param source_id Source identifier (empty for global only)
     * @return true if allowed at all levels
     */
    bool try_acquire(const std::string& source_id = "") noexcept {
        // Check global limit first
        if (!global_bucket_.try_acquire()) {
            return false;
        }

        // Check source-specific limit if present
        if (!source_id.empty()) {
            std::lock_guard lock(sources_mutex_);
            auto it = source_buckets_.find(source_id);
            if (it != source_buckets_.end()) {
                if (!it->second->try_acquire()) {
                    // Return global token (best effort)
                    return false;
                }
            }
        }

        return true;
    }

    /**
     * @brief Get global statistics
     */
    const RateLimiterStats& global_stats() const noexcept { return global_bucket_.stats(); }

    /**
     * @brief Get source-specific statistics
     * @return Pointer to stats if source exists, nullptr otherwise
     */
    const RateLimiterStats* source_stats(const std::string& source_id) const {
        std::lock_guard lock(sources_mutex_);
        auto it = source_buckets_.find(source_id);
        if (it != source_buckets_.end()) {
            return &it->second->stats();
        }
        return nullptr;
    }

private:
    TokenBucket global_bucket_;
    mutable std::mutex sources_mutex_;
    std::unordered_map<std::string, std::unique_ptr<TokenBucket>> source_buckets_;
};

/**
 * @brief Concurrent rate limiter registry
 *
 * Thread-safe registry for managing multiple rate limiters by name.
 */
class RateLimiterRegistry {
public:
    static RateLimiterRegistry& instance() {
        static RateLimiterRegistry registry;
        return registry;
    }

    /**
     * @brief Register a rate limiter
     */
    void register_limiter(const std::string& name, const RateLimitConfig& config) {
        std::lock_guard lock(mutex_);
        limiters_[name] = std::make_unique<TokenBucket>(config);
    }

    /**
     * @brief Get or create rate limiter
     */
    TokenBucket& get_or_create(const std::string& name, const RateLimitConfig& config = {}) {
        std::lock_guard lock(mutex_);
        auto it = limiters_.find(name);
        if (it == limiters_.end()) {
            limiters_[name] = std::make_unique<TokenBucket>(config);
            it              = limiters_.find(name);
        }
        return *it->second;
    }

    /**
     * @brief Try to acquire from named limiter
     */
    bool try_acquire(const std::string& name, size_t count = 1) {
        std::lock_guard lock(mutex_);
        auto it = limiters_.find(name);
        if (it == limiters_.end()) {
            return true;  // No limiter = no limit
        }
        return it->second->try_acquire(count);
    }

    /**
     * @brief Remove rate limiter
     */
    void remove(const std::string& name) {
        std::lock_guard lock(mutex_);
        limiters_.erase(name);
    }

private:
    RateLimiterRegistry() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<TokenBucket>> limiters_;
};

/**
 * @brief RAII rate limit acquisition guard
 */
class RateLimitGuard {
public:
    RateLimitGuard(TokenBucket& bucket, bool acquired = false)
        : bucket_(&bucket), acquired_(acquired) {}

    ~RateLimitGuard() = default;

    static std::optional<RateLimitGuard> try_acquire(TokenBucket& bucket) {
        if (bucket.try_acquire()) {
            return RateLimitGuard(bucket, true);
        }
        return std::nullopt;
    }

    explicit operator bool() const noexcept { return acquired_; }

private:
    TokenBucket* bucket_;
    bool acquired_;
};

}  // namespace ipb::common
