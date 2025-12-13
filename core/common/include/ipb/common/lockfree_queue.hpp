#pragma once

/**
 * @file lockfree_queue.hpp
 * @brief Lock-free queues for high-performance message passing
 *
 * Enterprise-grade lock-free data structures:
 * - SPSC Queue: Single Producer Single Consumer (fastest)
 * - MPSC Queue: Multiple Producers Single Consumer (common pattern)
 * - MPMC Queue: Multiple Producers Multiple Consumers (most flexible)
 *
 * Performance characteristics:
 * - Wait-free enqueue (bounded retry for MPMC)
 * - Lock-free dequeue
 * - Cache-line padding to prevent false sharing
 * - Memory ordering optimizations
 *
 * Use cases:
 * - SPSC: Dedicated sender/receiver threads (e.g., I/O to processing)
 * - MPSC: Multiple sources routing to single sink
 * - MPMC: General purpose work distribution
 */

#include <ipb/common/platform.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

namespace ipb::common {

// Cache line size for padding (typically 64 bytes on modern CPUs)
inline constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief Padding to prevent false sharing between cache lines
 */
template <typename T>
struct alignas(CACHE_LINE_SIZE) CacheLinePadded {
    T value;

    CacheLinePadded() = default;
    explicit CacheLinePadded(T v) : value(std::move(v)) {}

    operator T&() { return value; }
    operator const T&() const { return value; }
};

/**
 * @brief Statistics for lock-free queue monitoring
 */
struct LockFreeQueueStats {
    std::atomic<uint64_t> enqueues{0};
    std::atomic<uint64_t> dequeues{0};
    std::atomic<uint64_t> failed_enqueues{0};  // Queue was full
    std::atomic<uint64_t> failed_dequeues{0};  // Queue was empty
    std::atomic<uint64_t> spins{0};            // CAS retries

    void reset() noexcept {
        enqueues.store(0, std::memory_order_relaxed);
        dequeues.store(0, std::memory_order_relaxed);
        failed_enqueues.store(0, std::memory_order_relaxed);
        failed_dequeues.store(0, std::memory_order_relaxed);
        spins.store(0, std::memory_order_relaxed);
    }

    uint64_t size_approx() const noexcept {
        auto enq = enqueues.load(std::memory_order_relaxed);
        auto deq = dequeues.load(std::memory_order_relaxed);
        return enq > deq ? enq - deq : 0;
    }
};

/**
 * @brief Lock-free Single Producer Single Consumer queue
 *
 * The fastest lock-free queue variant. Use when you have exactly
 * one thread producing and one thread consuming.
 *
 * Performance:
 * - Enqueue: O(1), wait-free
 * - Dequeue: O(1), wait-free
 * - No atomic RMW operations in fast path
 *
 * @tparam T Element type (should be trivially copyable for best performance)
 */
template <typename T, size_t Capacity = 1024>
class SPSCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    SPSCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~SPSCQueue() = default;

    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&)                 = delete;
    SPSCQueue& operator=(SPSCQueue&&)      = delete;

    /**
     * @brief Enqueue an element (producer only)
     * @param value Value to enqueue
     * @return true if successful, false if queue is full
     */
    template <typename U>
    bool try_enqueue(U&& value) noexcept {
        const size_t pos = head_.load(std::memory_order_relaxed);
        Cell& cell       = buffer_[pos & MASK];

        const size_t seq = cell.sequence.load(std::memory_order_acquire);

        if (seq == pos) {
            // Slot is available
            cell.data = std::forward<U>(value);
            cell.sequence.store(pos + 1, std::memory_order_release);
            head_.store(pos + 1, std::memory_order_relaxed);
            stats_.enqueues.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        // Queue is full
        stats_.failed_enqueues.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /**
     * @brief Dequeue an element (consumer only)
     * @return Optional containing value if successful, empty if queue is empty
     */
    std::optional<T> try_dequeue() noexcept {
        const size_t pos = tail_.load(std::memory_order_relaxed);
        Cell& cell       = buffer_[pos & MASK];

        const size_t seq = cell.sequence.load(std::memory_order_acquire);

        if (seq == pos + 1) {
            // Data is available
            T result = std::move(cell.data);
            cell.sequence.store(pos + Capacity, std::memory_order_release);
            tail_.store(pos + 1, std::memory_order_relaxed);
            stats_.dequeues.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        // Queue is empty
        stats_.failed_dequeues.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    /**
     * @brief Check if queue is empty (approximate)
     */
    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get approximate size
     */
    size_t size_approx() const noexcept {
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_relaxed);
        return h >= t ? h - t : 0;
    }

    /**
     * @brief Get capacity
     */
    static constexpr size_t capacity() noexcept { return Capacity; }

    /**
     * @brief Get statistics
     */
    const LockFreeQueueStats& stats() const noexcept { return stats_; }

    /**
     * @brief Reset statistics
     */
    void reset_stats() noexcept { stats_.reset(); }

private:
    static constexpr size_t MASK = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    alignas(CACHE_LINE_SIZE) std::array<Cell, Capacity> buffer_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    alignas(CACHE_LINE_SIZE) mutable LockFreeQueueStats stats_;
};

/**
 * @brief Lock-free Multiple Producer Single Consumer queue
 *
 * Use when multiple threads produce data consumed by a single thread.
 * Common pattern for routing multiple data sources to a single sink.
 *
 * Performance:
 * - Enqueue: O(1) expected, wait-free with bounded retry
 * - Dequeue: O(1), wait-free
 *
 * @tparam T Element type
 */
template <typename T, size_t Capacity = 1024>
class MPSCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    MPSCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MPSCQueue() = default;

    // Non-copyable, non-movable
    MPSCQueue(const MPSCQueue&)            = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;
    MPSCQueue(MPSCQueue&&)                 = delete;
    MPSCQueue& operator=(MPSCQueue&&)      = delete;

    /**
     * @brief Enqueue an element (multiple producers)
     * @param value Value to enqueue
     * @return true if successful, false if queue is full
     */
    template <typename U>
    bool try_enqueue(U&& value) noexcept {
        Cell* cell;
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            cell          = &buffer_[pos & MASK];
            size_t seq    = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot is available, try to claim it
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
                    break;
                }
                stats_.spins.fetch_add(1, std::memory_order_relaxed);
            } else if (diff < 0) {
                // Queue is full
                stats_.failed_enqueues.fetch_add(1, std::memory_order_relaxed);
                return false;
            } else {
                // Another producer claimed this slot, retry
                pos = head_.load(std::memory_order_relaxed);
                stats_.spins.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Write data and publish
        cell->data = std::forward<U>(value);
        cell->sequence.store(pos + 1, std::memory_order_release);
        stats_.enqueues.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Dequeue an element (single consumer only)
     * @return Optional containing value if successful
     */
    std::optional<T> try_dequeue() noexcept {
        Cell* cell;
        size_t pos = tail_.load(std::memory_order_relaxed);

        cell          = &buffer_[pos & MASK];
        size_t seq    = cell->sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff == 0) {
            // Data is available
            T result = std::move(cell->data);
            cell->sequence.store(pos + Capacity, std::memory_order_release);
            tail_.store(pos + 1, std::memory_order_relaxed);
            stats_.dequeues.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        // Queue is empty
        stats_.failed_dequeues.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    /**
     * @brief Check if queue is empty (approximate)
     */
    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get approximate size
     */
    size_t size_approx() const noexcept {
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_relaxed);
        return h >= t ? h - t : 0;
    }

    static constexpr size_t capacity() noexcept { return Capacity; }
    const LockFreeQueueStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

private:
    static constexpr size_t MASK = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    alignas(CACHE_LINE_SIZE) std::array<Cell, Capacity> buffer_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    alignas(CACHE_LINE_SIZE) mutable LockFreeQueueStats stats_;
};

/**
 * @brief Lock-free Multiple Producer Multiple Consumer queue
 *
 * Most flexible variant, supporting any number of producers and consumers.
 * Use for general purpose work distribution.
 *
 * Performance:
 * - Enqueue: O(1) expected with bounded retry
 * - Dequeue: O(1) expected with bounded retry
 *
 * @tparam T Element type
 */
template <typename T, size_t Capacity = 1024>
class MPMCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    MPMCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MPMCQueue() = default;

    // Non-copyable, non-movable
    MPMCQueue(const MPMCQueue&)            = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    MPMCQueue(MPMCQueue&&)                 = delete;
    MPMCQueue& operator=(MPMCQueue&&)      = delete;

    /**
     * @brief Enqueue an element
     * @param value Value to enqueue
     * @return true if successful, false if queue is full
     */
    template <typename U>
    bool try_enqueue(U&& value) noexcept {
        Cell* cell;
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            cell          = &buffer_[pos & MASK];
            size_t seq    = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
                    break;
                }
                stats_.spins.fetch_add(1, std::memory_order_relaxed);
            } else if (diff < 0) {
                stats_.failed_enqueues.fetch_add(1, std::memory_order_relaxed);
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
                stats_.spins.fetch_add(1, std::memory_order_relaxed);
            }
        }

        cell->data = std::forward<U>(value);
        cell->sequence.store(pos + 1, std::memory_order_release);
        stats_.enqueues.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Dequeue an element
     * @return Optional containing value if successful
     */
    std::optional<T> try_dequeue() noexcept {
        Cell* cell;
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            cell          = &buffer_[pos & MASK];
            size_t seq    = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
                    break;
                }
                stats_.spins.fetch_add(1, std::memory_order_relaxed);
            } else if (diff < 0) {
                stats_.failed_dequeues.fetch_add(1, std::memory_order_relaxed);
                return std::nullopt;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
                stats_.spins.fetch_add(1, std::memory_order_relaxed);
            }
        }

        T result = std::move(cell->data);
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        stats_.dequeues.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    /**
     * @brief Blocking enqueue with spin-wait
     * @param value Value to enqueue
     * @param max_spins Maximum spin iterations before giving up
     * @return true if successful
     */
    template <typename U>
    bool enqueue(U&& value, size_t max_spins = 10000) noexcept {
        for (size_t i = 0; i < max_spins; ++i) {
            if (try_enqueue(std::forward<U>(value))) {
                return true;
            }
// Brief pause to reduce contention
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }
        return false;
    }

    /**
     * @brief Blocking dequeue with spin-wait
     * @param max_spins Maximum spin iterations before giving up
     * @return Optional containing value if successful
     */
    std::optional<T> dequeue(size_t max_spins = 10000) noexcept {
        for (size_t i = 0; i < max_spins; ++i) {
            auto result = try_dequeue();
            if (result) {
                return result;
            }
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }
        return std::nullopt;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    size_t size_approx() const noexcept {
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_relaxed);
        return h >= t ? h - t : 0;
    }

    static constexpr size_t capacity() noexcept { return Capacity; }
    const LockFreeQueueStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_.reset(); }

private:
    static constexpr size_t MASK = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    alignas(CACHE_LINE_SIZE) std::array<Cell, Capacity> buffer_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    alignas(CACHE_LINE_SIZE) mutable LockFreeQueueStats stats_;
};

/**
 * @brief Bounded lock-free queue with dynamic capacity
 *
 * Supports runtime-configurable capacity (must still be power of 2).
 * Slightly slower than fixed-capacity variants due to indirection.
 *
 * @tparam T Element type
 */
template <typename T>
class BoundedMPMCQueue {
public:
    explicit BoundedMPMCQueue(size_t capacity)
        : capacity_(next_power_of_2(capacity)), mask_(capacity_ - 1),
          buffer_(std::make_unique<Cell[]>(capacity_)) {
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~BoundedMPMCQueue() = default;

    // Non-copyable, non-movable
    BoundedMPMCQueue(const BoundedMPMCQueue&)            = delete;
    BoundedMPMCQueue& operator=(const BoundedMPMCQueue&) = delete;
    BoundedMPMCQueue(BoundedMPMCQueue&&)                 = delete;
    BoundedMPMCQueue& operator=(BoundedMPMCQueue&&)      = delete;

    template <typename U>
    bool try_enqueue(U&& value) noexcept {
        Cell* cell;
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            cell          = &buffer_[pos & mask_];
            size_t seq    = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::forward<U>(value);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> try_dequeue() noexcept {
        Cell* cell;
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            cell          = &buffer_[pos & mask_];
            size_t seq    = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return std::nullopt;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

        T result = std::move(cell->data);
        cell->sequence.store(pos + capacity_, std::memory_order_release);
        return result;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    size_t size_approx() const noexcept {
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_relaxed);
        return h >= t ? h - t : 0;
    }

    size_t capacity() const noexcept { return capacity_; }

private:
    static size_t next_power_of_2(size_t n) {
        if (n == 0)
            return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<Cell[]> buffer_;

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
};

}  // namespace ipb::common
