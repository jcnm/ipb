#pragma once

/**
 * @file cache_optimized.hpp
 * @brief Cache-optimized data structures for high-performance processing
 *
 * Enterprise-grade cache optimization features:
 * - Cache-line aligned containers for false sharing prevention
 * - Hot/cold data separation for improved locality
 * - Prefetch-friendly iteration patterns
 * - NUMA-aware memory layout hints
 * - Structure-of-Arrays (SoA) patterns for vectorization
 *
 * Performance characteristics:
 * - Reduced cache misses through alignment and padding
 * - Improved prefetcher effectiveness
 * - Better branch prediction through data-driven design
 */

#include <ipb/common/platform.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <thread>
#include <type_traits>
#include <vector>

namespace ipb::common {

/**
 * @brief Cache-line aligned value wrapper
 *
 * Prevents false sharing when multiple threads access adjacent values.
 * Uses padding to ensure each value occupies its own cache line.
 *
 * @tparam T Value type to wrap
 */
template<typename T>
struct alignas(IPB_CACHE_LINE_SIZE) CacheAligned {
    T value;

    CacheAligned() = default;
    explicit CacheAligned(const T& v) : value(v) {}
    explicit CacheAligned(T&& v) : value(std::move(v)) {}

    CacheAligned& operator=(const T& v) { value = v; return *this; }
    CacheAligned& operator=(T&& v) { value = std::move(v); return *this; }

    operator T&() noexcept { return value; }
    operator const T&() const noexcept { return value; }

    T* operator->() noexcept { return &value; }
    const T* operator->() const noexcept { return &value; }

private:
    // Padding to fill cache line
    char padding_[IPB_CACHE_LINE_SIZE - sizeof(T) > 0
                  ? IPB_CACHE_LINE_SIZE - sizeof(T)
                  : 1];
};

/**
 * @brief Double cache-line aligned value for avoiding prefetcher issues
 *
 * Some architectures prefetch two cache lines at once. This wrapper
 * ensures values don't share prefetch units with neighbors.
 */
template<typename T>
struct alignas(2 * IPB_CACHE_LINE_SIZE) DoubleCacheAligned {
    T value;

    DoubleCacheAligned() = default;
    explicit DoubleCacheAligned(const T& v) : value(v) {}
    explicit DoubleCacheAligned(T&& v) : value(std::move(v)) {}

    operator T&() noexcept { return value; }
    operator const T&() const noexcept { return value; }

private:
    char padding_[2 * IPB_CACHE_LINE_SIZE - sizeof(T) > 0
                  ? 2 * IPB_CACHE_LINE_SIZE - sizeof(T)
                  : 1];
};

/**
 * @brief Hot/cold data separation helper
 *
 * Separates frequently accessed (hot) data from rarely accessed (cold) data.
 * Hot data is kept in a cache-aligned block for better locality.
 *
 * @tparam HotData Frequently accessed data type
 * @tparam ColdData Rarely accessed data type
 */
template<typename HotData, typename ColdData>
struct alignas(IPB_CACHE_LINE_SIZE) HotColdSplit {
    // Hot data in first cache line(s)
    HotData hot;

    // Cold data follows, may span additional cache lines
    ColdData cold;

    HotColdSplit() = default;
    HotColdSplit(HotData h, ColdData c) : hot(std::move(h)), cold(std::move(c)) {}
};

/**
 * @brief Prefetch-friendly circular buffer
 *
 * Optimized for sequential access patterns with explicit prefetching.
 * Uses power-of-2 size for efficient modulo operations.
 *
 * @tparam T Element type
 * @tparam Capacity Buffer capacity (must be power of 2)
 */
template<typename T, size_t Capacity>
class alignas(IPB_CACHE_LINE_SIZE) PrefetchBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(Capacity > 0, "Capacity must be positive");

public:
    static constexpr size_t capacity = Capacity;
    static constexpr size_t mask = Capacity - 1;

    // Prefetch distance in elements (tune based on latency)
    static constexpr size_t prefetch_distance = 8;

    PrefetchBuffer() : head_(0), tail_(0) {}

    /**
     * @brief Push element with prefetch hint
     * @return true if successful, false if full
     */
    bool push(const T& value) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next = (tail + 1) & mask;

        if (next == head_.load(std::memory_order_acquire)) {
            return false; // Full
        }

        // Prefetch next write location
        if constexpr (prefetch_distance < Capacity) {
            size_t prefetch_idx = (tail + prefetch_distance) & mask;
            IPB_PREFETCH_WRITE(&data_[prefetch_idx]);
        }

        data_[tail] = value;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop element with prefetch hint
     * @return true if successful, false if empty
     */
    bool pop(T& value) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);

        if (head == tail_.load(std::memory_order_acquire)) {
            return false; // Empty
        }

        // Prefetch next read location
        if constexpr (prefetch_distance < Capacity) {
            size_t prefetch_idx = (head + prefetch_distance) & mask;
            IPB_PREFETCH_READ(&data_[prefetch_idx]);
        }

        value = data_[head];
        head_.store((head + 1) & mask, std::memory_order_release);
        return true;
    }

    /**
     * @brief Check if buffer is empty
     */
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get approximate size (may be stale in concurrent use)
     */
    size_t size() const noexcept {
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t head = head_.load(std::memory_order_acquire);
        return (tail - head) & mask;
    }

private:
    alignas(IPB_CACHE_LINE_SIZE) std::array<T, Capacity> data_;
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<size_t> head_;
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<size_t> tail_;
};

/**
 * @brief Structure-of-Arrays container for vectorization-friendly access
 *
 * Transforms Array-of-Structures (AoS) to Structure-of-Arrays (SoA)
 * layout for better cache utilization in SIMD operations.
 *
 * Example: Instead of [x1,y1,z1, x2,y2,z2, ...] store [x1,x2,..., y1,y2,..., z1,z2,...]
 *
 * @tparam Capacity Fixed capacity
 * @tparam Fields Field types
 */
template<size_t Capacity, typename... Fields>
class SoAContainer {
public:
    static constexpr size_t capacity = Capacity;
    static constexpr size_t field_count = sizeof...(Fields);

    SoAContainer() : size_(0) {}

    /**
     * @brief Add element by specifying all fields
     * @return Index of added element, or capacity if full
     */
    template<typename... Args>
    size_t push_back(Args&&... args) {
        static_assert(sizeof...(Args) == field_count, "Must provide all fields");

        if (size_ >= Capacity) {
            return Capacity; // Full
        }

        size_t idx = size_++;
        set_fields(idx, std::forward<Args>(args)...);
        return idx;
    }

    /**
     * @brief Get field array for vectorized processing
     */
    template<size_t FieldIndex>
    auto& get_field_array() noexcept {
        return std::get<FieldIndex>(arrays_);
    }

    template<size_t FieldIndex>
    const auto& get_field_array() const noexcept {
        return std::get<FieldIndex>(arrays_);
    }

    /**
     * @brief Get single element's field value
     */
    template<size_t FieldIndex>
    auto& get(size_t index) noexcept {
        return std::get<FieldIndex>(arrays_)[index];
    }

    template<size_t FieldIndex>
    const auto& get(size_t index) const noexcept {
        return std::get<FieldIndex>(arrays_)[index];
    }

    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ >= Capacity; }

    void clear() noexcept { size_ = 0; }

    /**
     * @brief Prefetch field arrays for batch processing
     */
    void prefetch_fields() const noexcept {
        prefetch_all(std::make_index_sequence<field_count>{});
    }

private:
    using Arrays = std::tuple<std::array<Fields, Capacity>...>;

    alignas(IPB_CACHE_LINE_SIZE) Arrays arrays_;
    size_t size_;

    template<typename First, typename... Rest>
    void set_fields(size_t idx, First&& first, Rest&&... rest) {
        constexpr size_t field_idx = field_count - sizeof...(Rest) - 1;
        std::get<field_idx>(arrays_)[idx] = std::forward<First>(first);

        if constexpr (sizeof...(Rest) > 0) {
            set_fields(idx, std::forward<Rest>(rest)...);
        }
    }

    void set_fields(size_t) {} // Base case

    template<size_t... Is>
    void prefetch_all(std::index_sequence<Is...>) const noexcept {
        (IPB_PREFETCH_READ(std::get<Is>(arrays_).data()), ...);
    }
};

/**
 * @brief Cache-aware batch processor
 *
 * Processes data in cache-line-sized batches for optimal performance.
 * Automatically handles prefetching and cache line boundaries.
 *
 * @tparam T Element type
 */
template<typename T>
class BatchProcessor {
public:
    // Elements per cache line
    static constexpr size_t elements_per_line = IPB_CACHE_LINE_SIZE / sizeof(T);
    static constexpr size_t prefetch_lines = 4; // Prefetch ahead distance

    /**
     * @brief Process array in cache-optimized batches
     * @param data Data array
     * @param count Number of elements
     * @param processor Function to process each element
     */
    template<typename Func>
    static void process(T* IPB_RESTRICT data, size_t count, Func&& processor) {
        // Process in cache-line batches
        size_t full_batches = count / elements_per_line;

        for (size_t batch = 0; batch < full_batches; ++batch) {
            // Prefetch future batches
            if (batch + prefetch_lines < full_batches) {
                IPB_PREFETCH_READ(&data[(batch + prefetch_lines) * elements_per_line]);
            }

            // Process current batch
            T* batch_ptr = &data[batch * elements_per_line];
            for (size_t i = 0; i < elements_per_line; ++i) {
                processor(batch_ptr[i]);
            }
        }

        // Process remaining elements
        size_t remaining_start = full_batches * elements_per_line;
        for (size_t i = remaining_start; i < count; ++i) {
            processor(data[i]);
        }
    }

    /**
     * @brief Process two arrays in parallel (useful for transformations)
     */
    template<typename U, typename Func>
    static void process_parallel(const T* IPB_RESTRICT input,
                                  U* IPB_RESTRICT output,
                                  size_t count,
                                  Func&& processor) {
        size_t full_batches = count / elements_per_line;

        for (size_t batch = 0; batch < full_batches; ++batch) {
            // Prefetch input and output
            if (batch + prefetch_lines < full_batches) {
                size_t prefetch_offset = (batch + prefetch_lines) * elements_per_line;
                IPB_PREFETCH_READ(&input[prefetch_offset]);
                IPB_PREFETCH_WRITE(&output[prefetch_offset]);
            }

            // Process current batch
            size_t batch_start = batch * elements_per_line;
            for (size_t i = 0; i < elements_per_line; ++i) {
                output[batch_start + i] = processor(input[batch_start + i]);
            }
        }

        // Process remaining
        size_t remaining_start = full_batches * elements_per_line;
        for (size_t i = remaining_start; i < count; ++i) {
            output[i] = processor(input[i]);
        }
    }
};

/**
 * @brief Intrusive list node with cache-line alignment
 *
 * For building cache-optimized linked data structures.
 */
template<typename T>
struct alignas(IPB_CACHE_LINE_SIZE) CacheAlignedNode {
    T data;
    CacheAlignedNode* next{nullptr};
    CacheAlignedNode* prev{nullptr};

    CacheAlignedNode() = default;
    explicit CacheAlignedNode(const T& d) : data(d) {}
    explicit CacheAlignedNode(T&& d) : data(std::move(d)) {}
};

/**
 * @brief Per-CPU data structure helper
 *
 * Creates per-CPU copies of data to avoid cache coherency traffic.
 * Useful for counters, statistics, and thread-local caching.
 *
 * @tparam T Data type (should be small, cache-line sized)
 * @tparam MaxCPUs Maximum number of CPUs supported
 */
template<typename T, size_t MaxCPUs = 128>
class alignas(IPB_CACHE_LINE_SIZE) PerCPUData {
public:
    PerCPUData() = default;

    explicit PerCPUData(const T& init) {
        for (auto& slot : data_) {
            slot.value = init;
        }
    }

    /**
     * @brief Get data for current CPU (approximation using thread ID)
     */
    T& local() noexcept {
        size_t slot = get_slot();
        return data_[slot].value;
    }

    const T& local() const noexcept {
        size_t slot = get_slot();
        return data_[slot].value;
    }

    /**
     * @brief Get data for specific slot
     */
    T& at(size_t slot) noexcept {
        return data_[slot % MaxCPUs].value;
    }

    const T& at(size_t slot) const noexcept {
        return data_[slot % MaxCPUs].value;
    }

    /**
     * @brief Aggregate all per-CPU values
     */
    template<typename Reducer>
    T reduce(Reducer&& reducer) const {
        T result = data_[0].value;
        for (size_t i = 1; i < MaxCPUs; ++i) {
            result = reducer(result, data_[i].value);
        }
        return result;
    }

    /**
     * @brief Sum all per-CPU values (for numeric types)
     */
    T sum() const {
        return reduce([](const T& a, const T& b) { return a + b; });
    }

    static constexpr size_t max_cpus = MaxCPUs;

private:
    CacheAligned<T> data_[MaxCPUs];

    static size_t get_slot() noexcept {
        // Simple hash of thread ID - not perfect CPU affinity but good approximation
        static thread_local size_t cached_slot =
            std::hash<std::thread::id>{}(std::this_thread::get_id()) % MaxCPUs;
        return cached_slot;
    }
};

/**
 * @brief Cache statistics collector
 *
 * Tracks cache performance metrics for tuning.
 */
struct CacheStats {
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> accesses{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> hits{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> misses{0};
    alignas(IPB_CACHE_LINE_SIZE) std::atomic<uint64_t> evictions{0};

    void record_hit() noexcept {
        accesses.fetch_add(1, std::memory_order_relaxed);
        hits.fetch_add(1, std::memory_order_relaxed);
    }

    void record_miss() noexcept {
        accesses.fetch_add(1, std::memory_order_relaxed);
        misses.fetch_add(1, std::memory_order_relaxed);
    }

    void record_eviction() noexcept {
        evictions.fetch_add(1, std::memory_order_relaxed);
    }

    double hit_rate() const noexcept {
        auto total = accesses.load(std::memory_order_relaxed);
        auto h = hits.load(std::memory_order_relaxed);
        return total > 0 ? static_cast<double>(h) / total * 100.0 : 0.0;
    }

    void reset() noexcept {
        accesses.store(0, std::memory_order_relaxed);
        hits.store(0, std::memory_order_relaxed);
        misses.store(0, std::memory_order_relaxed);
        evictions.store(0, std::memory_order_relaxed);
    }
};

/**
 * @brief Memory access pattern analyzer (for debugging/tuning)
 */
class AccessPatternTracker {
public:
    static constexpr size_t HISTORY_SIZE = 64;

    void record_access(const void* addr) noexcept {
        uintptr_t line = reinterpret_cast<uintptr_t>(addr) / IPB_CACHE_LINE_SIZE;
        history_[index_++ % HISTORY_SIZE] = line;
    }

    /**
     * @brief Detect sequential access pattern
     * @return true if accesses are mostly sequential
     */
    bool is_sequential() const noexcept {
        if (index_ < 2) return false;

        size_t sequential = 0;
        size_t count = std::min(index_, HISTORY_SIZE);

        for (size_t i = 1; i < count; ++i) {
            size_t prev_idx = (index_ - count + i - 1) % HISTORY_SIZE;
            size_t curr_idx = (index_ - count + i) % HISTORY_SIZE;

            if (history_[curr_idx] == history_[prev_idx] + 1) {
                ++sequential;
            }
        }

        return sequential > count / 2;
    }

    /**
     * @brief Get stride pattern (0 if irregular)
     */
    size_t detect_stride() const noexcept {
        if (index_ < 3) return 0;

        size_t count = std::min(index_, HISTORY_SIZE);
        int64_t first_diff = static_cast<int64_t>(history_[1]) -
                             static_cast<int64_t>(history_[0]);

        size_t consistent = 0;
        for (size_t i = 2; i < count; ++i) {
            int64_t diff = static_cast<int64_t>(history_[i]) -
                          static_cast<int64_t>(history_[i-1]);
            if (diff == first_diff) {
                ++consistent;
            }
        }

        return (consistent > count / 2) ? static_cast<size_t>(std::abs(first_diff)) : 0;
    }

private:
    std::array<uintptr_t, HISTORY_SIZE> history_{};
    size_t index_{0};
};

} // namespace ipb::common
