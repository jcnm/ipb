/**
 * @file memory_pool.cpp
 * @brief Implementation of tiered memory pool
 */

#include <ipb/common/memory_pool.hpp>

namespace ipb::common {

// ============================================================================
// TieredMemoryPool Implementation
// ============================================================================

TieredMemoryPool::TieredMemoryPool(size_t initial_capacity_per_tier)
    : small_pool_(initial_capacity_per_tier), medium_pool_(initial_capacity_per_tier),
      large_pool_(initial_capacity_per_tier) {}

TieredMemoryPool::~TieredMemoryPool() = default;

void* TieredMemoryPool::allocate(size_t size) {
    if (size <= SMALL_SIZE) {
        auto* block = small_pool_.allocate();
        stats_.small.allocations.fetch_add(1, std::memory_order_relaxed);
        stats_.small.pool_hits.fetch_add(1, std::memory_order_relaxed);
        return block->data;
    }

    if (size <= MEDIUM_SIZE) {
        auto* block = medium_pool_.allocate();
        stats_.medium.allocations.fetch_add(1, std::memory_order_relaxed);
        stats_.medium.pool_hits.fetch_add(1, std::memory_order_relaxed);
        return block->data;
    }

    if (size <= LARGE_SIZE) {
        auto* block = large_pool_.allocate();
        stats_.large.allocations.fetch_add(1, std::memory_order_relaxed);
        stats_.large.pool_hits.fetch_add(1, std::memory_order_relaxed);
        return block->data;
    }

    // Huge allocation - use heap
    stats_.huge_allocations.fetch_add(1, std::memory_order_relaxed);
    return ::operator new(size);
}

void TieredMemoryPool::deallocate(void* ptr, size_t size) {
    if (ptr == nullptr)
        return;

    if (size <= SMALL_SIZE) {
        auto* block = reinterpret_cast<SmallBlock*>(ptr);
        small_pool_.deallocate(block);
        stats_.small.deallocations.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (size <= MEDIUM_SIZE) {
        auto* block = reinterpret_cast<MediumBlock*>(ptr);
        medium_pool_.deallocate(block);
        stats_.medium.deallocations.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (size <= LARGE_SIZE) {
        auto* block = reinterpret_cast<LargeBlock*>(ptr);
        large_pool_.deallocate(block);
        stats_.large.deallocations.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Huge allocation - heap free
    stats_.huge_deallocations.fetch_add(1, std::memory_order_relaxed);
    ::operator delete(ptr);
}

// ============================================================================
// GlobalMemoryPool Implementation
// ============================================================================

TieredMemoryPool& GlobalMemoryPool::instance() {
    static TieredMemoryPool pool(1024);  // Pre-allocate 1024 objects per tier
    return pool;
}

}  // namespace ipb::common
