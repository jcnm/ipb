/**
 * @file test_lockfree_queue.cpp
 * @brief Comprehensive tests for lock-free queue implementations
 *
 * Tests cover:
 * - SPSCQueue (Single Producer Single Consumer)
 * - MPSCQueue (Multiple Producers Single Consumer)
 * - MPMCQueue (Multiple Producers Multiple Consumers)
 * - BoundedMPMCQueue (Dynamic capacity MPMC)
 * - LockFreeQueueStats
 * - Thread safety and concurrency
 * - Performance characteristics
 */

#include <ipb/common/lockfree_queue.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <numeric>
#include <random>
#include <set>
#include <thread>
#include <vector>

using namespace ipb::common;

// ============================================================================
// CacheLinePadded Tests
// ============================================================================

class CacheLinePaddedTest : public ::testing::Test {};

TEST_F(CacheLinePaddedTest, DefaultConstruction) {
    // CacheLinePadded uses default initialization, value is uninitialized
    // Just test that it can be constructed
    CacheLinePadded<int> padded{};  // Value-initialize to 0
    EXPECT_EQ(padded.value, 0);
}

TEST_F(CacheLinePaddedTest, ValueConstruction) {
    CacheLinePadded<int> padded(42);
    EXPECT_EQ(padded.value, 42);
}

TEST_F(CacheLinePaddedTest, ImplicitConversion) {
    CacheLinePadded<int> padded(42);
    int val = padded;
    EXPECT_EQ(val, 42);
}

TEST_F(CacheLinePaddedTest, Alignment) {
    EXPECT_EQ(alignof(CacheLinePadded<int>), CACHE_LINE_SIZE);
    EXPECT_EQ(alignof(CacheLinePadded<double>), CACHE_LINE_SIZE);
}

// ============================================================================
// LockFreeQueueStats Tests
// ============================================================================

class LockFreeQueueStatsTest : public ::testing::Test {
protected:
    LockFreeQueueStats stats;
};

TEST_F(LockFreeQueueStatsTest, InitialValues) {
    EXPECT_EQ(stats.enqueues.load(), 0);
    EXPECT_EQ(stats.dequeues.load(), 0);
    EXPECT_EQ(stats.failed_enqueues.load(), 0);
    EXPECT_EQ(stats.failed_dequeues.load(), 0);
    EXPECT_EQ(stats.spins.load(), 0);
}

TEST_F(LockFreeQueueStatsTest, IncrementOperations) {
    stats.enqueues.fetch_add(10);
    stats.dequeues.fetch_add(5);
    stats.failed_enqueues.fetch_add(2);
    stats.failed_dequeues.fetch_add(3);
    stats.spins.fetch_add(100);

    EXPECT_EQ(stats.enqueues.load(), 10);
    EXPECT_EQ(stats.dequeues.load(), 5);
    EXPECT_EQ(stats.failed_enqueues.load(), 2);
    EXPECT_EQ(stats.failed_dequeues.load(), 3);
    EXPECT_EQ(stats.spins.load(), 100);
}

TEST_F(LockFreeQueueStatsTest, SizeApprox) {
    stats.enqueues.store(100);
    stats.dequeues.store(30);
    EXPECT_EQ(stats.size_approx(), 70);
}

TEST_F(LockFreeQueueStatsTest, SizeApproxWhenEmpty) {
    stats.enqueues.store(50);
    stats.dequeues.store(50);
    EXPECT_EQ(stats.size_approx(), 0);
}

TEST_F(LockFreeQueueStatsTest, SizeApproxWhenDequeuesExceed) {
    stats.enqueues.store(10);
    stats.dequeues.store(20);  // Edge case
    EXPECT_EQ(stats.size_approx(), 0);
}

TEST_F(LockFreeQueueStatsTest, Reset) {
    stats.enqueues.store(100);
    stats.dequeues.store(50);
    stats.failed_enqueues.store(10);
    stats.failed_dequeues.store(20);
    stats.spins.store(500);

    stats.reset();

    EXPECT_EQ(stats.enqueues.load(), 0);
    EXPECT_EQ(stats.dequeues.load(), 0);
    EXPECT_EQ(stats.failed_enqueues.load(), 0);
    EXPECT_EQ(stats.failed_dequeues.load(), 0);
    EXPECT_EQ(stats.spins.load(), 0);
}

// ============================================================================
// SPSCQueue Tests
// ============================================================================

class SPSCQueueTest : public ::testing::Test {
protected:
    SPSCQueue<int, 16> queue;
};

TEST_F(SPSCQueueTest, InitiallyEmpty) {
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size_approx(), 0);
}

TEST_F(SPSCQueueTest, Capacity) {
    EXPECT_EQ(queue.capacity(), 16);
}

TEST_F(SPSCQueueTest, EnqueueSingle) {
    EXPECT_TRUE(queue.try_enqueue(42));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size_approx(), 1);
}

TEST_F(SPSCQueueTest, DequeueSingle) {
    queue.try_enqueue(42);
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
    EXPECT_TRUE(queue.empty());
}

TEST_F(SPSCQueueTest, DequeueEmpty) {
    auto result = queue.try_dequeue();
    EXPECT_FALSE(result.has_value());
}

TEST_F(SPSCQueueTest, EnqueueDequeueMultiple) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.try_enqueue(i));
    }
    EXPECT_EQ(queue.size_approx(), 10);

    for (int i = 0; i < 10; ++i) {
        auto result = queue.try_dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, i);
    }
    EXPECT_TRUE(queue.empty());
}

TEST_F(SPSCQueueTest, EnqueueFull) {
    // Fill the queue
    for (size_t i = 0; i < queue.capacity(); ++i) {
        EXPECT_TRUE(queue.try_enqueue(static_cast<int>(i)));
    }

    // Should fail when full
    EXPECT_FALSE(queue.try_enqueue(999));
}

TEST_F(SPSCQueueTest, FIFOOrder) {
    std::vector<int> input = {1, 2, 3, 4, 5};
    for (int val : input) {
        queue.try_enqueue(val);
    }

    std::vector<int> output;
    while (auto result = queue.try_dequeue()) {
        output.push_back(*result);
    }

    EXPECT_EQ(input, output);
}

TEST_F(SPSCQueueTest, StatsTracking) {
    queue.try_enqueue(1);
    queue.try_enqueue(2);
    queue.try_dequeue();
    queue.try_dequeue();
    queue.try_dequeue();  // Fail

    const auto& stats = queue.stats();
    EXPECT_EQ(stats.enqueues.load(), 2);
    EXPECT_EQ(stats.dequeues.load(), 2);
    EXPECT_EQ(stats.failed_dequeues.load(), 1);
}

TEST_F(SPSCQueueTest, ResetStats) {
    queue.try_enqueue(1);
    queue.try_dequeue();
    queue.reset_stats();

    const auto& stats = queue.stats();
    EXPECT_EQ(stats.enqueues.load(), 0);
    EXPECT_EQ(stats.dequeues.load(), 0);
}

TEST_F(SPSCQueueTest, MoveSemantics) {
    struct MoveOnly {
        int value = 0;
        bool moved = false;
        MoveOnly() = default;
        MoveOnly(int v) : value(v) {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&& other) noexcept : value(other.value), moved(true) {
            other.value = -1;
        }
        MoveOnly& operator=(MoveOnly&& other) noexcept {
            value = other.value;
            moved = true;
            other.value = -1;
            return *this;
        }
    };

    SPSCQueue<MoveOnly, 4> move_queue;
    move_queue.try_enqueue(MoveOnly(42));

    auto result = move_queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 42);
}

// ============================================================================
// MPSCQueue Tests
// ============================================================================

class MPSCQueueTest : public ::testing::Test {
protected:
    MPSCQueue<int, 64> queue;
};

TEST_F(MPSCQueueTest, InitiallyEmpty) {
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size_approx(), 0);
}

TEST_F(MPSCQueueTest, Capacity) {
    EXPECT_EQ(queue.capacity(), 64);
}

TEST_F(MPSCQueueTest, SingleProducerSingleConsumer) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(queue.try_enqueue(i));
    }

    for (int i = 0; i < 50; ++i) {
        auto result = queue.try_dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, i);
    }
}

TEST_F(MPSCQueueTest, MultipleProducersSingleConsumer) {
    const int NUM_PRODUCERS = 4;
    const int ITEMS_PER_PRODUCER = 100;
    std::vector<std::thread> producers;
    std::atomic<int> produced_count{0};

    MPSCQueue<int, 1024> large_queue;

    // Start producers
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&large_queue, &produced_count, p, ITEMS_PER_PRODUCER]() {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int value = p * ITEMS_PER_PRODUCER + i;
                while (!large_queue.try_enqueue(value)) {
                    std::this_thread::yield();
                }
                produced_count.fetch_add(1);
            }
        });
    }

    // Wait for producers
    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(produced_count.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);

    // Consume all items
    std::set<int> consumed;
    while (auto result = large_queue.try_dequeue()) {
        consumed.insert(*result);
    }

    EXPECT_EQ(consumed.size(), static_cast<size_t>(NUM_PRODUCERS * ITEMS_PER_PRODUCER));
}

// ============================================================================
// MPMCQueue Tests
// ============================================================================

class MPMCQueueTest : public ::testing::Test {
protected:
    MPMCQueue<int, 128> queue;
};

TEST_F(MPMCQueueTest, InitiallyEmpty) {
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size_approx(), 0);
}

TEST_F(MPMCQueueTest, Capacity) {
    EXPECT_EQ(queue.capacity(), 128);
}

TEST_F(MPMCQueueTest, SingleProducerSingleConsumer) {
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(queue.try_enqueue(i));
    }

    for (int i = 0; i < 100; ++i) {
        auto result = queue.try_dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, i);
    }
}

TEST_F(MPMCQueueTest, BlockingEnqueue) {
    MPMCQueue<int, 4> small_queue;

    // Fill queue
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(small_queue.enqueue(i, 100));
    }

    // Should fail after max_spins when full
    EXPECT_FALSE(small_queue.enqueue(999, 10));
}

TEST_F(MPMCQueueTest, BlockingDequeue) {
    MPMCQueue<int, 4> small_queue;

    // Empty queue - should fail after max_spins
    auto result = small_queue.dequeue(10);
    EXPECT_FALSE(result.has_value());

    // Add item and dequeue
    small_queue.try_enqueue(42);
    result = small_queue.dequeue(100);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_F(MPMCQueueTest, MultipleProducersMultipleConsumers) {
    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int ITEMS_PER_PRODUCER = 250;

    MPMCQueue<int, 1024> large_queue;
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Start producers
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&large_queue, &produced_count, p, ITEMS_PER_PRODUCER]() {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int value = p * ITEMS_PER_PRODUCER + i;
                while (!large_queue.try_enqueue(value)) {
                    std::this_thread::yield();
                }
                produced_count.fetch_add(1);
            }
        });
    }

    // Start consumers
    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        consumers.emplace_back([&large_queue, &consumed_count, &done]() {
            while (!done.load() || !large_queue.empty()) {
                if (large_queue.try_dequeue()) {
                    consumed_count.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Wait for producers
    for (auto& t : producers) {
        t.join();
    }

    done.store(true);

    // Wait for consumers
    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_EQ(produced_count.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_EQ(consumed_count.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
}

// ============================================================================
// BoundedMPMCQueue Tests
// ============================================================================

class BoundedMPMCQueueTest : public ::testing::Test {
protected:
    std::unique_ptr<BoundedMPMCQueue<int>> queue;

    void SetUp() override {
        queue = std::make_unique<BoundedMPMCQueue<int>>(64);
    }
};

TEST_F(BoundedMPMCQueueTest, InitiallyEmpty) {
    EXPECT_TRUE(queue->empty());
    EXPECT_EQ(queue->size_approx(), 0);
}

TEST_F(BoundedMPMCQueueTest, CapacityRoundedToPowerOf2) {
    BoundedMPMCQueue<int> q1(10);  // Should round to 16
    EXPECT_EQ(q1.capacity(), 16);

    BoundedMPMCQueue<int> q2(100);  // Should round to 128
    EXPECT_EQ(q2.capacity(), 128);

    BoundedMPMCQueue<int> q3(64);  // Already power of 2
    EXPECT_EQ(q3.capacity(), 64);
}

TEST_F(BoundedMPMCQueueTest, EnqueueDequeue) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(queue->try_enqueue(i));
    }

    for (int i = 0; i < 50; ++i) {
        auto result = queue->try_dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, i);
    }
}

TEST_F(BoundedMPMCQueueTest, FullQueue) {
    for (size_t i = 0; i < queue->capacity(); ++i) {
        EXPECT_TRUE(queue->try_enqueue(static_cast<int>(i)));
    }

    EXPECT_FALSE(queue->try_enqueue(999));
}

TEST_F(BoundedMPMCQueueTest, ConcurrentAccess) {
    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 1000;

    BoundedMPMCQueue<int> concurrent_queue(2048);
    std::atomic<int> enqueue_count{0};
    std::atomic<int> dequeue_count{0};
    std::vector<std::thread> threads;

    // Mixed producers and consumers
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&concurrent_queue, &enqueue_count, &dequeue_count, t, OPS_PER_THREAD]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                if (t % 2 == 0) {
                    // Producer
                    if (concurrent_queue.try_enqueue(i)) {
                        enqueue_count.fetch_add(1);
                    }
                } else {
                    // Consumer
                    if (concurrent_queue.try_dequeue()) {
                        dequeue_count.fetch_add(1);
                    }
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Drain remaining
    while (concurrent_queue.try_dequeue()) {
        dequeue_count.fetch_add(1);
    }

    EXPECT_EQ(enqueue_count.load(), dequeue_count.load());
}

// ============================================================================
// Performance Tests
// ============================================================================

class LockFreeQueuePerformanceTest : public ::testing::Test {};

TEST_F(LockFreeQueuePerformanceTest, SPSCThroughput) {
    SPSCQueue<int, 4096> queue;
    const int NUM_OPS = 100000;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&queue, NUM_OPS]() {
        for (int i = 0; i < NUM_OPS; ++i) {
            while (!queue.try_enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&queue, NUM_OPS]() {
        int count = 0;
        while (count < NUM_OPS) {
            if (queue.try_dequeue()) {
                ++count;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete 100K ops in reasonable time
    EXPECT_LT(duration.count(), 5000);  // < 5 seconds
}

TEST_F(LockFreeQueuePerformanceTest, MPMCThroughput) {
    MPMCQueue<int, 4096> queue;
    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int OPS_PER_THREAD = 10000;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;

    // Producers
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        threads.emplace_back([&queue, &produced, OPS_PER_THREAD]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                while (!queue.try_enqueue(i)) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1);
            }
        });
    }

    // Consumers
    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        threads.emplace_back([&queue, &consumed, &done]() {
            while (!done.load() || !queue.empty()) {
                if (queue.try_dequeue()) {
                    consumed.fetch_add(1);
                }
            }
        });
    }

    // Wait for producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        threads[i].join();
    }

    done.store(true);

    // Wait for consumers
    for (int i = NUM_PRODUCERS; i < NUM_PRODUCERS + NUM_CONSUMERS; ++i) {
        threads[i].join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(produced.load(), consumed.load());
    EXPECT_LT(duration.count(), 10000);  // < 10 seconds
}

// ============================================================================
// Edge Case Tests
// ============================================================================

class LockFreeQueueEdgeCasesTest : public ::testing::Test {};

TEST_F(LockFreeQueueEdgeCasesTest, SingleElementQueue) {
    SPSCQueue<int, 2> tiny_queue;  // Minimum power of 2

    EXPECT_TRUE(tiny_queue.try_enqueue(1));
    EXPECT_TRUE(tiny_queue.try_enqueue(2));
    EXPECT_FALSE(tiny_queue.try_enqueue(3));  // Full

    EXPECT_EQ(*tiny_queue.try_dequeue(), 1);
    EXPECT_EQ(*tiny_queue.try_dequeue(), 2);
    EXPECT_FALSE(tiny_queue.try_dequeue().has_value());  // Empty
}

TEST_F(LockFreeQueueEdgeCasesTest, LargeElements) {
    struct LargeStruct {
        std::array<char, 1024> data;
        int id;
    };

    SPSCQueue<LargeStruct, 8> queue;

    LargeStruct item{};
    item.id = 42;
    std::fill(item.data.begin(), item.data.end(), 'A');

    EXPECT_TRUE(queue.try_enqueue(item));
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, 42);
    EXPECT_EQ(result->data[0], 'A');
}

TEST_F(LockFreeQueueEdgeCasesTest, RapidEnqueueDequeue) {
    SPSCQueue<int, 4> queue;

    for (int round = 0; round < 1000; ++round) {
        queue.try_enqueue(round);
        auto result = queue.try_dequeue();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, round);
    }
}

TEST_F(LockFreeQueueEdgeCasesTest, WrapAround) {
    SPSCQueue<int, 4> queue;

    // Fill and empty multiple times to test wrap-around
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (int i = 0; i < 4; ++i) {
            EXPECT_TRUE(queue.try_enqueue(cycle * 4 + i));
        }

        for (int i = 0; i < 4; ++i) {
            auto result = queue.try_dequeue();
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(*result, cycle * 4 + i);
        }
    }
}

// ============================================================================
// String Element Tests
// ============================================================================

class LockFreeQueueStringTest : public ::testing::Test {};

TEST_F(LockFreeQueueStringTest, StringElements) {
    SPSCQueue<std::string, 16> queue;

    queue.try_enqueue("Hello");
    queue.try_enqueue("World");
    queue.try_enqueue(std::string(1000, 'X'));  // Long string

    EXPECT_EQ(*queue.try_dequeue(), "Hello");
    EXPECT_EQ(*queue.try_dequeue(), "World");
    EXPECT_EQ(queue.try_dequeue()->size(), 1000);
}

TEST_F(LockFreeQueueStringTest, MoveOnlyStrings) {
    MPMCQueue<std::unique_ptr<std::string>, 8> queue;

    queue.try_enqueue(std::make_unique<std::string>("test"));
    auto result = queue.try_dequeue();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(**result, "test");
}
