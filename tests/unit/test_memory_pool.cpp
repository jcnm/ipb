/**
 * @file test_memory_pool.cpp
 * @brief Comprehensive unit tests for ipb::common memory pool classes
 *
 * Tests cover:
 * - PoolStats struct
 * - ObjectPool<T, BlockSize>
 * - PooledPtr<T, Pool>
 * - TieredMemoryPool
 * - GlobalMemoryPool
 * - PoolAllocator<T>
 */

#include <ipb/common/memory_pool.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::common;

// ============================================================================
// Test Fixtures and Helper Types
// ============================================================================

struct TestObject {
    int value;
    std::string name;

    TestObject() : value(0), name("default") {}
    TestObject(int v) : value(v), name("value_" + std::to_string(v)) {}
    TestObject(int v, std::string n) : value(v), name(std::move(n)) {}

    ~TestObject() {
        // Destructor for testing proper cleanup
        value = -1;
    }
};

struct LargeObject {
    char data[512];
    int id;

    LargeObject() : id(0) { std::fill_n(data, sizeof(data), 0); }
    explicit LargeObject(int i) : id(i) { std::fill_n(data, sizeof(data), static_cast<char>(i)); }
};

// ============================================================================
// PoolStats Tests
// ============================================================================

class PoolStatsTest : public ::testing::Test {
protected:
    PoolStats stats_;
};

TEST_F(PoolStatsTest, DefaultValues) {
    EXPECT_EQ(stats_.allocations.load(), 0u);
    EXPECT_EQ(stats_.deallocations.load(), 0u);
    EXPECT_EQ(stats_.pool_hits.load(), 0u);
    EXPECT_EQ(stats_.pool_misses.load(), 0u);
    EXPECT_EQ(stats_.capacity.load(), 0u);
    EXPECT_EQ(stats_.in_use.load(), 0u);
    EXPECT_EQ(stats_.high_water_mark.load(), 0u);
}

TEST_F(PoolStatsTest, HitRateZeroTotal) {
    EXPECT_DOUBLE_EQ(stats_.hit_rate(), 0.0);
}

TEST_F(PoolStatsTest, HitRateWithData) {
    stats_.pool_hits.store(80);
    stats_.pool_misses.store(20);

    EXPECT_DOUBLE_EQ(stats_.hit_rate(), 80.0);
}

TEST_F(PoolStatsTest, HitRatePerfect) {
    stats_.pool_hits.store(100);
    stats_.pool_misses.store(0);

    EXPECT_DOUBLE_EQ(stats_.hit_rate(), 100.0);
}

TEST_F(PoolStatsTest, Reset) {
    stats_.allocations.store(100);
    stats_.deallocations.store(50);
    stats_.pool_hits.store(80);
    stats_.pool_misses.store(20);
    stats_.capacity.store(200);
    stats_.in_use.store(50);

    stats_.reset();

    EXPECT_EQ(stats_.allocations.load(), 0u);
    EXPECT_EQ(stats_.deallocations.load(), 0u);
    EXPECT_EQ(stats_.pool_hits.load(), 0u);
    EXPECT_EQ(stats_.pool_misses.load(), 0u);
    // capacity and in_use should NOT be reset
    EXPECT_EQ(stats_.capacity.load(), 200u);
    EXPECT_EQ(stats_.in_use.load(), 50u);
}

// ============================================================================
// ObjectPool Basic Tests
// ============================================================================

class ObjectPoolBasicTest : public ::testing::Test {
protected:
    ObjectPool<TestObject> pool_;
};

TEST_F(ObjectPoolBasicTest, DefaultConstruction) {
    EXPECT_EQ(pool_.capacity(), 0u);
    EXPECT_EQ(pool_.in_use(), 0u);
    EXPECT_EQ(pool_.available(), 0u);
}

TEST_F(ObjectPoolBasicTest, ConstructWithInitialCapacity) {
    ObjectPool<TestObject> pool(100);
    EXPECT_GE(pool.capacity(), 64u);  // At least one block (BlockSize = 64)
}

TEST_F(ObjectPoolBasicTest, Reserve) {
    pool_.reserve(200);
    EXPECT_GE(pool_.capacity(), 128u);  // At least 2 blocks
}

TEST_F(ObjectPoolBasicTest, AllocateDefault) {
    TestObject* obj = pool_.allocate();

    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->value, 0);
    EXPECT_EQ(obj->name, "default");
    EXPECT_EQ(pool_.in_use(), 1u);

    pool_.deallocate(obj);
}

TEST_F(ObjectPoolBasicTest, AllocateWithArgs) {
    TestObject* obj = pool_.allocate(42);

    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->value, 42);
    EXPECT_EQ(obj->name, "value_42");

    pool_.deallocate(obj);
}

TEST_F(ObjectPoolBasicTest, AllocateWithMultipleArgs) {
    TestObject* obj = pool_.allocate(99, "custom_name");

    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->value, 99);
    EXPECT_EQ(obj->name, "custom_name");

    pool_.deallocate(obj);
}

TEST_F(ObjectPoolBasicTest, DeallocateNull) {
    // Should not crash
    pool_.deallocate(nullptr);

    EXPECT_EQ(pool_.in_use(), 0u);
}

TEST_F(ObjectPoolBasicTest, Deallocate) {
    TestObject* obj = pool_.allocate(42);
    EXPECT_EQ(pool_.in_use(), 1u);

    pool_.deallocate(obj);
    EXPECT_EQ(pool_.in_use(), 0u);
}

TEST_F(ObjectPoolBasicTest, ReuseAfterDeallocate) {
    pool_.reserve(64);

    TestObject* obj1 = pool_.allocate(1);
    pool_.deallocate(obj1);

    TestObject* obj2 = pool_.allocate(2);

    // Object should be reused from pool
    EXPECT_EQ(obj1, obj2);
    EXPECT_EQ(obj2->value, 2);

    pool_.deallocate(obj2);
}

// ============================================================================
// ObjectPool Statistics Tests
// ============================================================================

class ObjectPoolStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_.reserve(128);
    }

    ObjectPool<TestObject> pool_;
};

TEST_F(ObjectPoolStatsTest, AllocationTracking) {
    const auto& stats = pool_.stats();

    TestObject* obj1 = pool_.allocate();
    EXPECT_EQ(stats.allocations.load(), 1u);

    TestObject* obj2 = pool_.allocate();
    EXPECT_EQ(stats.allocations.load(), 2u);

    pool_.deallocate(obj1);
    pool_.deallocate(obj2);
}

TEST_F(ObjectPoolStatsTest, DeallocationTracking) {
    const auto& stats = pool_.stats();

    TestObject* obj = pool_.allocate();
    pool_.deallocate(obj);

    EXPECT_EQ(stats.deallocations.load(), 1u);
}

TEST_F(ObjectPoolStatsTest, PoolHitsTracking) {
    const auto& stats = pool_.stats();

    TestObject* obj = pool_.allocate();
    EXPECT_GE(stats.pool_hits.load(), 1u);

    pool_.deallocate(obj);
}

TEST_F(ObjectPoolStatsTest, InUseTracking) {
    std::vector<TestObject*> objects;

    for (int i = 0; i < 10; ++i) {
        objects.push_back(pool_.allocate(i));
    }
    EXPECT_EQ(pool_.in_use(), 10u);

    for (auto* obj : objects) {
        pool_.deallocate(obj);
    }
    EXPECT_EQ(pool_.in_use(), 0u);
}

TEST_F(ObjectPoolStatsTest, HighWaterMark) {
    const auto& stats = pool_.stats();

    std::vector<TestObject*> objects;
    for (int i = 0; i < 50; ++i) {
        objects.push_back(pool_.allocate(i));
    }

    // Deallocate half
    for (int i = 0; i < 25; ++i) {
        pool_.deallocate(objects[i]);
    }

    EXPECT_EQ(stats.high_water_mark.load(), 50u);
    EXPECT_EQ(pool_.in_use(), 25u);

    // Cleanup
    for (int i = 25; i < 50; ++i) {
        pool_.deallocate(objects[i]);
    }
}

TEST_F(ObjectPoolStatsTest, ResetStats) {
    TestObject* obj = pool_.allocate();
    pool_.deallocate(obj);

    pool_.reset_stats();

    const auto& stats = pool_.stats();
    EXPECT_EQ(stats.allocations.load(), 0u);
    EXPECT_EQ(stats.deallocations.load(), 0u);
    EXPECT_EQ(stats.pool_hits.load(), 0u);
    EXPECT_EQ(stats.pool_misses.load(), 0u);
}

TEST_F(ObjectPoolStatsTest, CapacityAfterReserve) {
    pool_.reserve(256);
    EXPECT_GE(pool_.capacity(), 192u);  // At least 3 blocks
}

TEST_F(ObjectPoolStatsTest, Available) {
    TestObject* obj1 = pool_.allocate();
    TestObject* obj2 = pool_.allocate();

    size_t available_before = pool_.available();

    pool_.deallocate(obj1);

    size_t available_after = pool_.available();
    EXPECT_GT(available_after, available_before);

    pool_.deallocate(obj2);
}

// ============================================================================
// ObjectPool Multithreaded Tests
// ============================================================================

class ObjectPoolMultithreadedTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_.reserve(1024);
    }

    ObjectPool<TestObject, 128> pool_;
};

TEST_F(ObjectPoolMultithreadedTest, ConcurrentAllocations) {
    const int num_threads = 4;
    const int allocations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &success_count, allocations_per_thread]() {
            std::vector<TestObject*> objects;

            for (int i = 0; i < allocations_per_thread; ++i) {
                TestObject* obj = pool_.allocate(i);
                if (obj) {
                    objects.push_back(obj);
                    success_count++;
                }
            }

            for (auto* obj : objects) {
                pool_.deallocate(obj);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * allocations_per_thread);
    EXPECT_EQ(pool_.in_use(), 0u);
}

TEST_F(ObjectPoolMultithreadedTest, ConcurrentAllocDealloc) {
    const int num_threads = 8;
    const int operations = 50;

    std::vector<std::thread> threads;
    std::atomic<bool> start{false};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &start, operations]() {
            while (!start.load()) {
                std::this_thread::yield();
            }

            for (int i = 0; i < operations; ++i) {
                TestObject* obj = pool_.allocate(i);
                // Brief work
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                pool_.deallocate(obj);
            }
        });
    }

    start.store(true);

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(pool_.in_use(), 0u);
}

// ============================================================================
// PooledPtr Tests
// ============================================================================

class PooledPtrTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_.reserve(64);
    }

    ObjectPool<TestObject> pool_;
};

TEST_F(PooledPtrTest, DefaultConstruction) {
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr;

    EXPECT_FALSE(ptr);
    EXPECT_EQ(ptr.get(), nullptr);
}

TEST_F(PooledPtrTest, ConstructFromAllocate) {
    TestObject* raw = pool_.allocate(42);
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr(raw, &pool_);

    EXPECT_TRUE(ptr);
    EXPECT_NE(ptr.get(), nullptr);
    EXPECT_EQ(ptr->value, 42);
}

TEST_F(PooledPtrTest, Dereference) {
    TestObject* raw = pool_.allocate(99, "test");
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr(raw, &pool_);

    EXPECT_EQ((*ptr).value, 99);
    EXPECT_EQ((*ptr).name, "test");
}

TEST_F(PooledPtrTest, ArrowOperator) {
    TestObject* raw = pool_.allocate(77);
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr(raw, &pool_);

    EXPECT_EQ(ptr->value, 77);
}

TEST_F(PooledPtrTest, MoveConstruction) {
    TestObject* raw = pool_.allocate(42);
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr1(raw, &pool_);

    PooledPtr<TestObject, ObjectPool<TestObject>> ptr2(std::move(ptr1));

    EXPECT_FALSE(ptr1);
    EXPECT_TRUE(ptr2);
    EXPECT_EQ(ptr2->value, 42);
}

TEST_F(PooledPtrTest, MoveAssignment) {
    TestObject* raw1 = pool_.allocate(1);
    TestObject* raw2 = pool_.allocate(2);

    PooledPtr<TestObject, ObjectPool<TestObject>> ptr1(raw1, &pool_);
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr2(raw2, &pool_);

    ptr2 = std::move(ptr1);

    EXPECT_FALSE(ptr1);
    EXPECT_TRUE(ptr2);
    EXPECT_EQ(ptr2->value, 1);
}

TEST_F(PooledPtrTest, Release) {
    TestObject* raw = pool_.allocate(42);
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr(raw, &pool_);

    TestObject* released = ptr.release();

    EXPECT_FALSE(ptr);
    EXPECT_EQ(released, raw);
    EXPECT_EQ(released->value, 42);

    // Manual cleanup since we released
    pool_.deallocate(released);
}

TEST_F(PooledPtrTest, Reset) {
    TestObject* raw = pool_.allocate(42);
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr(raw, &pool_);

    EXPECT_EQ(pool_.in_use(), 1u);

    ptr.reset();

    EXPECT_FALSE(ptr);
    EXPECT_EQ(pool_.in_use(), 0u);
}

TEST_F(PooledPtrTest, AutomaticCleanup) {
    EXPECT_EQ(pool_.in_use(), 0u);

    {
        TestObject* raw = pool_.allocate(42);
        PooledPtr<TestObject, ObjectPool<TestObject>> ptr(raw, &pool_);
        EXPECT_EQ(pool_.in_use(), 1u);
    }

    EXPECT_EQ(pool_.in_use(), 0u);
}

TEST_F(PooledPtrTest, SelfAssignment) {
    TestObject* raw = pool_.allocate(42);
    PooledPtr<TestObject, ObjectPool<TestObject>> ptr(raw, &pool_);

    // Test self-assignment via indirection to avoid -Wself-move warning
    // This is an intentional test to verify PooledPtr handles self-move correctly
    auto& ref = ptr;
    ptr       = std::move(ref);

    // Should still be valid after self-assignment
    EXPECT_TRUE(ptr);
    EXPECT_EQ(ptr->value, 42);

    // Explicit reset to cleanup
    ptr.reset();
}

// ============================================================================
// TieredMemoryPool Tests
// ============================================================================

class TieredMemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<TieredMemoryPool>(64);
    }

    std::unique_ptr<TieredMemoryPool> pool_;
};

TEST_F(TieredMemoryPoolTest, SmallAllocation) {
    void* ptr = pool_->allocate(32);
    ASSERT_NE(ptr, nullptr);

    pool_->deallocate(ptr, 32);
}

TEST_F(TieredMemoryPoolTest, MediumAllocation) {
    void* ptr = pool_->allocate(128);
    ASSERT_NE(ptr, nullptr);

    pool_->deallocate(ptr, 128);
}

TEST_F(TieredMemoryPoolTest, LargeAllocation) {
    void* ptr = pool_->allocate(512);
    ASSERT_NE(ptr, nullptr);

    pool_->deallocate(ptr, 512);
}

TEST_F(TieredMemoryPoolTest, HugeAllocation) {
    void* ptr = pool_->allocate(4096);
    ASSERT_NE(ptr, nullptr);

    pool_->deallocate(ptr, 4096);
}

TEST_F(TieredMemoryPoolTest, MultipleAllocations) {
    std::vector<std::pair<void*, size_t>> allocations;

    // Mix of sizes
    std::vector<size_t> sizes = {16, 64, 128, 256, 512, 1024, 2048};

    for (size_t size : sizes) {
        void* ptr = pool_->allocate(size);
        ASSERT_NE(ptr, nullptr);
        allocations.push_back({ptr, size});
    }

    for (auto& [ptr, size] : allocations) {
        pool_->deallocate(ptr, size);
    }
}

TEST_F(TieredMemoryPoolTest, Stats) {
    const auto& stats = pool_->stats();

    void* small = pool_->allocate(32);
    void* medium = pool_->allocate(128);
    void* large = pool_->allocate(512);
    void* huge = pool_->allocate(4096);

    EXPECT_GE(stats.small.allocations.load(), 1u);
    EXPECT_GE(stats.medium.allocations.load(), 1u);
    EXPECT_GE(stats.large.allocations.load(), 1u);
    EXPECT_GE(stats.huge_allocations.load(), 1u);

    pool_->deallocate(small, 32);
    pool_->deallocate(medium, 128);
    pool_->deallocate(large, 512);
    pool_->deallocate(huge, 4096);
}

// ============================================================================
// GlobalMemoryPool Tests
// ============================================================================

class GlobalMemoryPoolTest : public ::testing::Test {};

TEST_F(GlobalMemoryPoolTest, SingletonInstance) {
    auto& pool1 = GlobalMemoryPool::instance();
    auto& pool2 = GlobalMemoryPool::instance();

    EXPECT_EQ(&pool1, &pool2);
}

TEST_F(GlobalMemoryPoolTest, BasicUsage) {
    auto& pool = GlobalMemoryPool::instance();

    void* ptr = pool.allocate(64);
    ASSERT_NE(ptr, nullptr);

    pool.deallocate(ptr, 64);
}

// ============================================================================
// PoolAllocator Tests
// ============================================================================

class PoolAllocatorTest : public ::testing::Test {};

TEST_F(PoolAllocatorTest, VectorWithPoolAllocator) {
    std::vector<int, PoolAllocator<int>> vec;

    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }

    EXPECT_EQ(vec.size(), 100u);

    int expected = 0;
    for (int val : vec) {
        EXPECT_EQ(val, expected++);
    }
}

TEST_F(PoolAllocatorTest, StringWithPoolAllocator) {
    using PoolString = std::basic_string<char, std::char_traits<char>, PoolAllocator<char>>;

    PoolString str = "Hello, World!";
    EXPECT_EQ(str.size(), 13u);
}

TEST_F(PoolAllocatorTest, AllocatorEquality) {
    PoolAllocator<int> alloc1;
    PoolAllocator<int> alloc2;
    PoolAllocator<double> alloc3;

    EXPECT_TRUE(alloc1 == alloc2);
    EXPECT_TRUE(alloc1 == alloc3);
    EXPECT_FALSE(alloc1 != alloc2);
}

TEST_F(PoolAllocatorTest, RebindAllocator) {
    PoolAllocator<int> int_alloc;
    PoolAllocator<double> double_alloc(int_alloc);

    // Should compile and be equal
    EXPECT_TRUE(int_alloc == double_alloc);
}

// ============================================================================
// ObjectPool with Different Block Sizes
// ============================================================================

class ObjectPoolBlockSizeTest : public ::testing::Test {};

TEST_F(ObjectPoolBlockSizeTest, SmallBlockSize) {
    ObjectPool<TestObject, 8> pool(32);

    std::vector<TestObject*> objects;
    for (int i = 0; i < 20; ++i) {
        objects.push_back(pool.allocate(i));
    }

    EXPECT_EQ(pool.in_use(), 20u);

    for (auto* obj : objects) {
        pool.deallocate(obj);
    }
}

TEST_F(ObjectPoolBlockSizeTest, LargeBlockSize) {
    ObjectPool<TestObject, 256> pool(512);

    std::vector<TestObject*> objects;
    for (int i = 0; i < 300; ++i) {
        objects.push_back(pool.allocate(i));
    }

    EXPECT_EQ(pool.in_use(), 300u);

    for (auto* obj : objects) {
        pool.deallocate(obj);
    }
}

// ============================================================================
// ObjectPool with Large Objects
// ============================================================================

class ObjectPoolLargeObjectTest : public ::testing::Test {
protected:
    ObjectPool<LargeObject, 16> pool_;
};

TEST_F(ObjectPoolLargeObjectTest, AllocateAndDeallocate) {
    LargeObject* obj = pool_.allocate(42);

    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->id, 42);

    pool_.deallocate(obj);
}

TEST_F(ObjectPoolLargeObjectTest, MultipleAllocations) {
    std::vector<LargeObject*> objects;

    for (int i = 0; i < 50; ++i) {
        objects.push_back(pool_.allocate(i));
    }

    // Verify all objects
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(objects[i]->id, i);
    }

    for (auto* obj : objects) {
        pool_.deallocate(obj);
    }
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

class ObjectPoolEdgeCaseTest : public ::testing::Test {};

TEST_F(ObjectPoolEdgeCaseTest, HeapFallback) {
    // Create pool without reserving
    ObjectPool<TestObject, 4> pool;

    // Allocate more than initial capacity
    std::vector<TestObject*> objects;
    for (int i = 0; i < 100; ++i) {
        objects.push_back(pool.allocate(i));
    }

    // All allocations should succeed (pool may expand or use heap)
    const auto& stats = pool.stats();
    EXPECT_EQ(stats.allocations.load(), 100u);
    // Note: pool_misses depends on implementation - pool may auto-expand
    // Just verify all objects were allocated successfully
    EXPECT_EQ(objects.size(), 100u);
    for (auto* obj : objects) {
        EXPECT_NE(obj, nullptr);
    }

    for (auto* obj : objects) {
        pool.deallocate(obj);
    }
}

TEST_F(ObjectPoolEdgeCaseTest, RapidAllocDealloc) {
    ObjectPool<TestObject> pool(64);

    for (int i = 0; i < 1000; ++i) {
        TestObject* obj = pool.allocate(i);
        EXPECT_NE(obj, nullptr);
        pool.deallocate(obj);
    }

    EXPECT_EQ(pool.in_use(), 0u);
}

TEST_F(ObjectPoolEdgeCaseTest, ZeroCapacityReserve) {
    ObjectPool<TestObject> pool;
    pool.reserve(0);

    // Should still be able to allocate (will trigger block allocation)
    TestObject* obj = pool.allocate();
    EXPECT_NE(obj, nullptr);

    pool.deallocate(obj);
}
