#pragma once

// MSVC: Disable C4324 warning for intentional cache-line padding
// This warning is expected as we deliberately use alignas() to prevent false sharing
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // structure was padded due to alignment specifier
#endif

/**
 * @file memory_pool.hpp
 * @brief High-performance memory pooling for zero-allocation hot paths
 *
 * Enterprise-grade memory management features:
 * - Thread-safe object pooling with lock-free fast path
 * - Pre-allocated memory blocks for known traffic patterns
 * - Multiple pool tiers for different object sizes
 * - Statistics and monitoring for capacity planning
 * - RAII wrapper for automatic return to pool
 *
 * Performance characteristics:
 * - Allocation: O(1) lock-free (fast path), O(1) with lock (slow path)
 * - Deallocation: O(1) lock-free
 * - Memory overhead: ~16 bytes per object + block header
 */

#include <ipb/common/platform.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace ipb::common {

/**
 * @brief Statistics for memory pool monitoring
 */
struct PoolStats {
    std::atomic<uint64_t> allocations{0};
    std::atomic<uint64_t> deallocations{0};
    std::atomic<uint64_t> pool_hits{0};        // Got from pool
    std::atomic<uint64_t> pool_misses{0};      // Had to allocate new
    std::atomic<uint64_t> capacity{0};         // Total objects in pool
    std::atomic<uint64_t> in_use{0};           // Currently checked out
    std::atomic<uint64_t> high_water_mark{0};  // Peak in_use

    double hit_rate() const noexcept {
        auto total = pool_hits.load() + pool_misses.load();
        return total > 0 ? static_cast<double>(pool_hits.load()) / total * 100.0 : 0.0;
    }

    void reset() noexcept {
        allocations.store(0);
        deallocations.store(0);
        pool_hits.store(0);
        pool_misses.store(0);
        // Don't reset capacity/in_use - they reflect actual state
    }
};

/**
 * @brief Lock-free object pool for single object type
 *
 * Uses a lock-free stack for fast allocation/deallocation.
 * Falls back to heap allocation when pool is exhausted.
 *
 * @tparam T Object type to pool
 * @tparam BlockSize Number of objects per allocation block
 */
template <typename T, size_t BlockSize = 64>
class ObjectPool {
public:
    /**
     * @brief Construct pool with initial capacity
     * @param initial_capacity Pre-allocate this many objects
     */
    explicit ObjectPool(size_t initial_capacity = 0) {
        if (initial_capacity > 0) {
            reserve(initial_capacity);
        }
    }

    ~ObjectPool() {
        // Free all blocks
        std::lock_guard lock(blocks_mutex_);
        for (auto& block : blocks_) {
            ::operator delete(block.memory, std::align_val_t{alignof(T)});
        }
    }

    // Non-copyable, non-movable (contains atomics)
    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&)                 = delete;
    ObjectPool& operator=(ObjectPool&&)      = delete;

    /**
     * @brief Pre-allocate objects
     * @param count Number of objects to pre-allocate
     */
    void reserve(size_t count) {
        size_t blocks_needed = (count + BlockSize - 1) / BlockSize;
        std::lock_guard lock(blocks_mutex_);

        for (size_t i = 0; i < blocks_needed; ++i) {
            allocate_block();
        }

        stats_.capacity.store(blocks_.size() * BlockSize, std::memory_order_relaxed);
    }

    /**
     * @brief Allocate object from pool
     * @param args Constructor arguments
     * @return Pointer to constructed object
     *
     * Tries pool first (lock-free), falls back to heap.
     */
    template <typename... Args>
    T* allocate(Args&&... args) {
        stats_.allocations.fetch_add(1, std::memory_order_relaxed);

        // Try to get from free list (lock-free)
        Node* node = free_list_.load(std::memory_order_acquire);
        while (node != nullptr) {
            Node* next = node->next.load(std::memory_order_relaxed);
            if (free_list_.compare_exchange_weak(node, next, std::memory_order_release,
                                                 std::memory_order_relaxed)) {
                stats_.pool_hits.fetch_add(1, std::memory_order_relaxed);
                update_in_use(1);

                // Construct in place
                return new (node) T(std::forward<Args>(args)...);
            }
        }

        // Pool exhausted - allocate new block or individual object
        {
            std::lock_guard lock(blocks_mutex_);
            if (should_allocate_block()) {
                allocate_block();
                stats_.capacity.store(blocks_.size() * BlockSize, std::memory_order_relaxed);

                // Try free list again after allocating block
                node = free_list_.load(std::memory_order_acquire);
                if (node != nullptr) {
                    Node* next = node->next.load(std::memory_order_relaxed);
                    if (free_list_.compare_exchange_strong(node, next)) {
                        stats_.pool_hits.fetch_add(1, std::memory_order_relaxed);
                        update_in_use(1);
                        return new (node) T(std::forward<Args>(args)...);
                    }
                }
            }
        }

        // Fall back to heap
        stats_.pool_misses.fetch_add(1, std::memory_order_relaxed);
        update_in_use(1);

        void* mem = ::operator new(sizeof(T), std::align_val_t{alignof(T)});
        return new (mem) T(std::forward<Args>(args)...);
    }

    /**
     * @brief Return object to pool
     * @param ptr Object to return (must have been allocated from this pool or heap)
     */
    void deallocate(T* ptr) {
        if (ptr == nullptr)
            return;

        stats_.deallocations.fetch_add(1, std::memory_order_relaxed);
        update_in_use(-1);

        // Destroy object
        ptr->~T();

        // Check if this came from our blocks
        bool from_pool = is_from_pool(ptr);

        if (from_pool) {
            // Return to free list (lock-free)
            Node* node     = reinterpret_cast<Node*>(ptr);
            Node* expected = free_list_.load(std::memory_order_relaxed);
            do {
                node->next.store(expected, std::memory_order_relaxed);
            } while (!free_list_.compare_exchange_weak(expected, node, std::memory_order_release,
                                                       std::memory_order_relaxed));
        } else {
            // Was heap allocated
            ::operator delete(ptr, std::align_val_t{alignof(T)});
        }
    }

    /**
     * @brief Get pool statistics
     */
    const PoolStats& stats() const noexcept { return stats_; }

    /**
     * @brief Reset statistics (doesn't affect pool state)
     */
    void reset_stats() noexcept { stats_.reset(); }

    /**
     * @brief Get current capacity (total objects that can be pooled)
     */
    size_t capacity() const noexcept { return stats_.capacity.load(std::memory_order_relaxed); }

    /**
     * @brief Get number of objects currently in use
     */
    size_t in_use() const noexcept { return stats_.in_use.load(std::memory_order_relaxed); }

    /**
     * @brief Get number of objects available in pool
     */
    size_t available() const noexcept {
        auto cap  = capacity();
        auto used = in_use();
        return cap > used ? cap - used : 0;
    }

private:
    // Free list node (reuses object memory)
    struct Node {
        std::atomic<Node*> next{nullptr};
        alignas(T) char storage[sizeof(T)];
    };

    // Memory block
    struct Block {
        void* memory;
        size_t size;
        uintptr_t start;  // First address in block
        uintptr_t end;    // Last address in block + 1
    };

    std::atomic<Node*> free_list_{nullptr};
    mutable PoolStats stats_;

    mutable std::mutex blocks_mutex_;
    std::vector<Block> blocks_;

    void allocate_block() {
        // Allocate aligned memory for BlockSize objects
        constexpr size_t object_size = std::max(sizeof(Node), sizeof(T));
        const size_t block_size      = object_size * BlockSize;

        void* memory = ::operator new(block_size, std::align_val_t{alignof(T)});

        Block block;
        block.memory = memory;
        block.size   = block_size;
        block.start  = reinterpret_cast<uintptr_t>(memory);
        block.end    = block.start + block_size;
        blocks_.push_back(block);

        // Add all objects to free list
        for (size_t i = 0; i < BlockSize; ++i) {
            Node* node = reinterpret_cast<Node*>(static_cast<char*>(memory) + i * object_size);

            Node* expected = free_list_.load(std::memory_order_relaxed);
            do {
                node->next.store(expected, std::memory_order_relaxed);
            } while (!free_list_.compare_exchange_weak(expected, node, std::memory_order_release,
                                                       std::memory_order_relaxed));
        }
    }

    bool is_from_pool(void* ptr) const {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        std::lock_guard<std::mutex> lock(blocks_mutex_);

        for (const auto& block : blocks_) {
            if (addr >= block.start && addr < block.end) {
                return true;
            }
        }
        return false;
    }

    bool should_allocate_block() const {
        // Allocate new block if pool is running low
        return blocks_.empty() || free_list_.load(std::memory_order_relaxed) == nullptr;
    }

    void update_in_use(int64_t delta) {
        uint64_t current   = stats_.in_use.fetch_add(delta, std::memory_order_relaxed);
        uint64_t new_value = static_cast<uint64_t>(static_cast<int64_t>(current) + delta);

        // Update high water mark
        uint64_t hwm = stats_.high_water_mark.load(std::memory_order_relaxed);
        while (new_value > hwm) {
            if (stats_.high_water_mark.compare_exchange_weak(hwm, new_value)) {
                break;
            }
        }
    }
};

/**
 * @brief RAII wrapper for pooled objects
 *
 * Automatically returns object to pool when destroyed.
 *
 * @tparam T Object type
 * @tparam Pool Pool type (ObjectPool<T, ...>)
 */
template <typename T, typename Pool>
class PooledPtr {
public:
    PooledPtr() noexcept : ptr_(nullptr), pool_(nullptr) {}

    PooledPtr(T* ptr, Pool* pool) noexcept : ptr_(ptr), pool_(pool) {}

    ~PooledPtr() { reset(); }

    // Non-copyable
    PooledPtr(const PooledPtr&)            = delete;
    PooledPtr& operator=(const PooledPtr&) = delete;

    // Movable
    PooledPtr(PooledPtr&& other) noexcept : ptr_(other.ptr_), pool_(other.pool_) {
        other.ptr_  = nullptr;
        other.pool_ = nullptr;
    }

    PooledPtr& operator=(PooledPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_        = other.ptr_;
            pool_       = other.pool_;
            other.ptr_  = nullptr;
            other.pool_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T* release() noexcept {
        T* p  = ptr_;
        ptr_  = nullptr;
        pool_ = nullptr;
        return p;
    }

    void reset() {
        if (ptr_ && pool_) {
            pool_->deallocate(ptr_);
        }
        ptr_  = nullptr;
        pool_ = nullptr;
    }

private:
    T* ptr_;
    Pool* pool_;
};

/**
 * @brief Multi-tier memory pool for variable-size allocations
 *
 * Uses different pools for different size classes:
 * - Small: <= 64 bytes
 * - Medium: <= 256 bytes
 * - Large: <= 1024 bytes
 * - Huge: heap allocated
 */
class TieredMemoryPool {
public:
    explicit TieredMemoryPool(size_t initial_capacity_per_tier = 256);
    ~TieredMemoryPool();

    // Non-copyable
    TieredMemoryPool(const TieredMemoryPool&)            = delete;
    TieredMemoryPool& operator=(const TieredMemoryPool&) = delete;

    /**
     * @brief Allocate memory of given size
     * @param size Bytes to allocate
     * @return Pointer to allocated memory
     */
    void* allocate(size_t size);

    /**
     * @brief Deallocate memory
     * @param ptr Pointer to memory
     * @param size Size that was allocated (must match)
     */
    void deallocate(void* ptr, size_t size);

    /**
     * @brief Get statistics for all tiers
     */
    struct TieredStats {
        PoolStats small;   // <= 64 bytes
        PoolStats medium;  // <= 256 bytes
        PoolStats large;   // <= 1024 bytes
        std::atomic<uint64_t> huge_allocations{0};
        std::atomic<uint64_t> huge_deallocations{0};
    };

    const TieredStats& stats() const noexcept { return stats_; }

private:
    // Size tiers
    static constexpr size_t SMALL_SIZE  = 64;
    static constexpr size_t MEDIUM_SIZE = 256;
    static constexpr size_t LARGE_SIZE  = 1024;

    struct SmallBlock {
        alignas(64) char data[SMALL_SIZE];
    };
    struct MediumBlock {
        alignas(64) char data[MEDIUM_SIZE];
    };
    struct LargeBlock {
        alignas(64) char data[LARGE_SIZE];
    };

    ObjectPool<SmallBlock> small_pool_;
    ObjectPool<MediumBlock> medium_pool_;
    ObjectPool<LargeBlock> large_pool_;

    mutable TieredStats stats_;
};

/**
 * @brief Global singleton for application-wide memory pool
 */
class GlobalMemoryPool {
public:
    static TieredMemoryPool& instance();

    // Delete copy/move
    GlobalMemoryPool(const GlobalMemoryPool&)            = delete;
    GlobalMemoryPool& operator=(const GlobalMemoryPool&) = delete;

private:
    GlobalMemoryPool() = default;
};

/**
 * @brief Allocator adapter for STL containers using pool
 */
template <typename T>
class PoolAllocator {
public:
    using value_type                             = T;
    using size_type                              = std::size_t;
    using difference_type                        = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;

    PoolAllocator() noexcept = default;

    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    T* allocate(size_type n) {
        return static_cast<T*>(GlobalMemoryPool::instance().allocate(n * sizeof(T)));
    }

    void deallocate(T* p, size_type n) noexcept {
        GlobalMemoryPool::instance().deallocate(p, n * sizeof(T));
    }

    template <typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept {
        return true;
    }

    template <typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept {
        return false;
    }
};

}  // namespace ipb::common

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
