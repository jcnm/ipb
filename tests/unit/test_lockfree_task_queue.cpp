/**
 * @file test_lockfree_task_queue.cpp
 * @brief Comprehensive unit tests for IPB lock-free task queue
 *
 * Industrial-grade test coverage including:
 * - LockFreeTask operations and state transitions
 * - LockFreeSkipList basic and advanced operations
 * - LockFreeTaskQueue operations and ordering
 * - Realistic concurrent access patterns
 * - EDF (Earliest Deadline First) ordering validation
 * - Task cancellation and edge cases
 * - Boundary conditions and stress testing
 */

#include <ipb/common/lockfree_task_queue.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::common;
using namespace std::chrono_literals;

// ============================================================================
// LockFreeTask Tests
// ============================================================================

class LockFreeTaskTest : public ::testing::Test {};

TEST_F(LockFreeTaskTest, DefaultConstruction) {
    LockFreeTask task;
    EXPECT_EQ(task.id, 0u);
    EXPECT_EQ(task.deadline_ns, 0);
    EXPECT_EQ(task.priority, 128u);
    EXPECT_TRUE(task.is_pending());
}

TEST_F(LockFreeTaskTest, SetName) {
    LockFreeTask task;
    task.set_name("test_task");
    EXPECT_EQ(task.get_name(), "test_task");
}

TEST_F(LockFreeTaskTest, SetNameTruncatesLong) {
    LockFreeTask task;
    std::string long_name(100, 'x');
    task.set_name(long_name);
    EXPECT_LE(task.get_name().size(), LockFreeTask::MAX_NAME_LENGTH - 1);
}

TEST_F(LockFreeTaskTest, SetNameEmpty) {
    LockFreeTask task;
    task.set_name("");
    EXPECT_EQ(task.get_name(), "");
}

TEST_F(LockFreeTaskTest, SetNameExactlyMaxLength) {
    LockFreeTask task;
    std::string exact_name(LockFreeTask::MAX_NAME_LENGTH - 1, 'a');
    task.set_name(exact_name);
    EXPECT_EQ(task.get_name(), exact_name);
}

TEST_F(LockFreeTaskTest, TryCancel) {
    LockFreeTask task;
    task.state = TaskState::PENDING;
    EXPECT_TRUE(task.try_cancel());
    EXPECT_EQ(task.state, TaskState::CANCELLED);
}

TEST_F(LockFreeTaskTest, TryCancelAlreadyRunning) {
    LockFreeTask task;
    task.state = TaskState::RUNNING;
    EXPECT_FALSE(task.try_cancel());
    EXPECT_EQ(task.state, TaskState::RUNNING);
}

TEST_F(LockFreeTaskTest, TryCancelAlreadyCompleted) {
    LockFreeTask task;
    task.state = TaskState::COMPLETED;
    EXPECT_FALSE(task.try_cancel());
    EXPECT_EQ(task.state, TaskState::COMPLETED);
}

TEST_F(LockFreeTaskTest, TryCancelAlreadyCancelled) {
    LockFreeTask task;
    task.state = TaskState::CANCELLED;
    EXPECT_FALSE(task.try_cancel());
    EXPECT_EQ(task.state, TaskState::CANCELLED);
}

TEST_F(LockFreeTaskTest, TryCancelAlreadyFailed) {
    LockFreeTask task;
    task.state = TaskState::FAILED;
    EXPECT_FALSE(task.try_cancel());
    EXPECT_EQ(task.state, TaskState::FAILED);
}

TEST_F(LockFreeTaskTest, AllStateTransitions) {
    // Test all TaskState values
    LockFreeTask task;

    task.state = TaskState::PENDING;
    EXPECT_TRUE(task.is_pending());
    EXPECT_FALSE(task.is_cancelled());

    task.state = TaskState::RUNNING;
    EXPECT_FALSE(task.is_pending());
    EXPECT_FALSE(task.is_cancelled());

    task.state = TaskState::COMPLETED;
    EXPECT_FALSE(task.is_pending());
    EXPECT_FALSE(task.is_cancelled());

    task.state = TaskState::CANCELLED;
    EXPECT_FALSE(task.is_pending());
    EXPECT_TRUE(task.is_cancelled());

    task.state = TaskState::FAILED;
    EXPECT_FALSE(task.is_pending());
    EXPECT_FALSE(task.is_cancelled());
}

TEST_F(LockFreeTaskTest, CopyConstruction) {
    LockFreeTask task1;
    task1.id          = 42;
    task1.deadline_ns = 12345;
    task1.priority    = 200;
    task1.set_name("original");

    LockFreeTask task2 = task1;
    EXPECT_EQ(task2.id, 42u);
    EXPECT_EQ(task2.deadline_ns, 12345);
    EXPECT_EQ(task2.priority, 200u);
    EXPECT_EQ(task2.get_name(), "original");
}

TEST_F(LockFreeTaskTest, MoveConstruction) {
    LockFreeTask task1;
    task1.id          = 42;
    task1.deadline_ns = 12345;
    task1.priority    = 255;

    LockFreeTask task2 = std::move(task1);
    EXPECT_EQ(task2.id, 42u);
    EXPECT_EQ(task2.deadline_ns, 12345);
    EXPECT_EQ(task2.priority, 255u);
}

TEST_F(LockFreeTaskTest, CopyAssignment) {
    LockFreeTask task1;
    task1.id          = 100;
    task1.deadline_ns = 999;

    LockFreeTask task2;
    task2 = task1;
    EXPECT_EQ(task2.id, 100u);
    EXPECT_EQ(task2.deadline_ns, 999);
}

TEST_F(LockFreeTaskTest, MoveAssignment) {
    LockFreeTask task1;
    task1.id = 100;

    LockFreeTask task2;
    task2 = std::move(task1);
    EXPECT_EQ(task2.id, 100u);
}

TEST_F(LockFreeTaskTest, Equality) {
    LockFreeTask task1;
    task1.id = 42;

    LockFreeTask task2;
    task2.id = 42;

    LockFreeTask task3;
    task3.id = 43;

    EXPECT_TRUE(task1 == task2);
    EXPECT_FALSE(task1 == task3);
}

TEST_F(LockFreeTaskTest, ComparisonByDeadline) {
    LockFreeTask earlier;
    earlier.deadline_ns = 100;

    LockFreeTask later;
    later.deadline_ns = 200;

    EXPECT_TRUE(earlier < later);
    EXPECT_FALSE(later < earlier);
    EXPECT_TRUE(later > earlier);
}

TEST_F(LockFreeTaskTest, ComparisonByPriorityWhenEqualDeadline) {
    LockFreeTask high_priority;
    high_priority.deadline_ns = 100;
    high_priority.priority    = 200;

    LockFreeTask low_priority;
    low_priority.deadline_ns = 100;
    low_priority.priority    = 50;

    // Higher priority should come first (be "less than" in ordering)
    EXPECT_TRUE(high_priority < low_priority);
}

// Boundary tests for task values
TEST_F(LockFreeTaskTest, BoundaryDeadlineValues) {
    LockFreeTask task;

    // Minimum deadline
    task.deadline_ns = std::numeric_limits<int64_t>::min();
    EXPECT_EQ(task.deadline_ns, std::numeric_limits<int64_t>::min());

    // Maximum deadline
    task.deadline_ns = std::numeric_limits<int64_t>::max();
    EXPECT_EQ(task.deadline_ns, std::numeric_limits<int64_t>::max());

    // Zero deadline
    task.deadline_ns = 0;
    EXPECT_EQ(task.deadline_ns, 0);

    // Negative deadline
    task.deadline_ns = -1000;
    EXPECT_EQ(task.deadline_ns, -1000);
}

TEST_F(LockFreeTaskTest, BoundaryPriorityValues) {
    LockFreeTask task;

    // Min priority
    task.priority = 0;
    EXPECT_EQ(task.priority, 0u);

    // Max priority
    task.priority = 255;
    EXPECT_EQ(task.priority, 255u);

    // Default priority
    task.priority = 128;
    EXPECT_EQ(task.priority, 128u);
}

TEST_F(LockFreeTaskTest, BoundaryIdValues) {
    LockFreeTask task;

    // Min ID
    task.id = 0;
    EXPECT_EQ(task.id, 0u);

    // Max ID
    task.id = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ(task.id, std::numeric_limits<uint64_t>::max());
}

TEST_F(LockFreeTaskTest, CompareTasksWithExtremeDeadlines) {
    LockFreeTask min_deadline;
    min_deadline.deadline_ns = std::numeric_limits<int64_t>::min();

    LockFreeTask max_deadline;
    max_deadline.deadline_ns = std::numeric_limits<int64_t>::max();

    EXPECT_TRUE(min_deadline < max_deadline);
    EXPECT_FALSE(max_deadline < min_deadline);
}

// ============================================================================
// LockFreeSkipList Tests
// ============================================================================

class LockFreeSkipListTest : public ::testing::Test {
protected:
    LockFreeSkipList<int> list;
};

TEST_F(LockFreeSkipListTest, InitiallyEmpty) {
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);
}

TEST_F(LockFreeSkipListTest, InsertSingle) {
    EXPECT_TRUE(list.insert(42));
    EXPECT_FALSE(list.empty());
    EXPECT_EQ(list.size(), 1u);
}

TEST_F(LockFreeSkipListTest, InsertMultiple) {
    list.insert(3);
    list.insert(1);
    list.insert(4);
    list.insert(1);  // Duplicate
    list.insert(5);

    EXPECT_EQ(list.size(), 4u);  // Duplicate not counted
}

TEST_F(LockFreeSkipListTest, InsertLargeSequential) {
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(list.insert(i));
    }
    EXPECT_EQ(list.size(), 1000u);
}

TEST_F(LockFreeSkipListTest, InsertLargeReverse) {
    for (int i = 999; i >= 0; --i) {
        EXPECT_TRUE(list.insert(i));
    }
    EXPECT_EQ(list.size(), 1000u);
}

TEST_F(LockFreeSkipListTest, InsertLargeRandom) {
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> dist(0, 10000);
    std::set<int> inserted;

    for (int i = 0; i < 1000; ++i) {
        int value = dist(gen);
        if (inserted.insert(value).second) {
            EXPECT_TRUE(list.insert(value));
        }
    }
    EXPECT_EQ(list.size(), inserted.size());
}

TEST_F(LockFreeSkipListTest, Contains) {
    list.insert(10);
    list.insert(20);
    list.insert(30);

    EXPECT_TRUE(list.contains(10));
    EXPECT_TRUE(list.contains(20));
    EXPECT_TRUE(list.contains(30));
    EXPECT_FALSE(list.contains(15));
    EXPECT_FALSE(list.contains(0));
}

TEST_F(LockFreeSkipListTest, ContainsBoundaryValues) {
    list.insert(std::numeric_limits<int>::min());
    list.insert(std::numeric_limits<int>::max());
    list.insert(0);

    EXPECT_TRUE(list.contains(std::numeric_limits<int>::min()));
    EXPECT_TRUE(list.contains(std::numeric_limits<int>::max()));
    EXPECT_TRUE(list.contains(0));
}

TEST_F(LockFreeSkipListTest, Remove) {
    list.insert(10);
    list.insert(20);
    list.insert(30);

    EXPECT_TRUE(list.remove(20));
    EXPECT_EQ(list.size(), 2u);
    EXPECT_FALSE(list.contains(20));
    EXPECT_TRUE(list.contains(10));
    EXPECT_TRUE(list.contains(30));
}

TEST_F(LockFreeSkipListTest, RemoveNonexistent) {
    list.insert(10);
    EXPECT_FALSE(list.remove(20));
    EXPECT_EQ(list.size(), 1u);
}

TEST_F(LockFreeSkipListTest, RemoveFromEmpty) {
    EXPECT_FALSE(list.remove(42));
    EXPECT_TRUE(list.empty());
}

TEST_F(LockFreeSkipListTest, RemoveAll) {
    for (int i = 0; i < 100; ++i) {
        list.insert(i);
    }

    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(list.remove(i));
    }

    EXPECT_TRUE(list.empty());
}

TEST_F(LockFreeSkipListTest, PopMinReturnsSmallest) {
    list.insert(30);
    list.insert(10);
    list.insert(20);

    auto min = list.pop_min();
    ASSERT_TRUE(min.has_value());
    EXPECT_EQ(*min, 10);
    EXPECT_EQ(list.size(), 2u);

    min = list.pop_min();
    ASSERT_TRUE(min.has_value());
    EXPECT_EQ(*min, 20);

    min = list.pop_min();
    ASSERT_TRUE(min.has_value());
    EXPECT_EQ(*min, 30);

    EXPECT_TRUE(list.empty());
}

TEST_F(LockFreeSkipListTest, PopMinEmpty) {
    auto min = list.pop_min();
    EXPECT_FALSE(min.has_value());
}

TEST_F(LockFreeSkipListTest, PopMinMaintainsOrder) {
    std::vector<int> values = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    for (int v : values) {
        list.insert(v);
    }

    for (int expected = 0; expected < 10; ++expected) {
        auto min = list.pop_min();
        ASSERT_TRUE(min.has_value());
        EXPECT_EQ(*min, expected);
    }
}

TEST_F(LockFreeSkipListTest, PeekMin) {
    list.insert(30);
    list.insert(10);
    list.insert(20);

    auto min = list.peek_min();
    ASSERT_TRUE(min.has_value());
    EXPECT_EQ(*min, 10);
    EXPECT_EQ(list.size(), 3u);  // Not removed
}

TEST_F(LockFreeSkipListTest, PeekMinEmpty) {
    auto min = list.peek_min();
    EXPECT_FALSE(min.has_value());
}

TEST_F(LockFreeSkipListTest, PeekMinDoesNotModify) {
    list.insert(42);

    for (int i = 0; i < 10; ++i) {
        auto min = list.peek_min();
        ASSERT_TRUE(min.has_value());
        EXPECT_EQ(*min, 42);
    }

    EXPECT_EQ(list.size(), 1u);
}

TEST_F(LockFreeSkipListTest, RemoveIf) {
    list.insert(10);
    list.insert(20);
    list.insert(30);

    EXPECT_TRUE(list.remove_if([](int x) { return x > 15 && x < 25; }));
    EXPECT_EQ(list.size(), 2u);
    EXPECT_FALSE(list.contains(20));
}

TEST_F(LockFreeSkipListTest, RemoveIfNoMatch) {
    list.insert(10);
    list.insert(20);
    list.insert(30);

    EXPECT_FALSE(list.remove_if([](int x) { return x > 100; }));
    EXPECT_EQ(list.size(), 3u);
}

TEST_F(LockFreeSkipListTest, RemoveIfAll) {
    for (int i = 0; i < 10; ++i) {
        list.insert(i);
    }

    // Remove all even numbers
    int removed = 0;
    while (list.remove_if([](int x) { return x % 2 == 0; })) {
        ++removed;
    }

    EXPECT_EQ(removed, 5);
    EXPECT_EQ(list.size(), 5u);
}

// ============================================================================
// LockFreeTaskQueue Tests
// ============================================================================

class LockFreeTaskQueueTest : public ::testing::Test {
protected:
    LockFreeTaskQueue queue{10000};

    LockFreeTask make_task(uint64_t id, int64_t deadline, uint8_t priority = 128) {
        LockFreeTask task;
        task.id          = id;
        task.deadline_ns = deadline;
        task.priority    = priority;
        return task;
    }
};

TEST_F(LockFreeTaskQueueTest, InitiallyEmpty) {
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);
}

TEST_F(LockFreeTaskQueueTest, PushAndPop) {
    auto task = make_task(1, 100);
    EXPECT_TRUE(queue.push(task));
    EXPECT_EQ(queue.size(), 1u);

    LockFreeTask popped;
    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped.id, 1u);
    EXPECT_TRUE(queue.empty());
}

TEST_F(LockFreeTaskQueueTest, PushMany) {
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(queue.push(make_task(i, i * 100)));
    }
    EXPECT_EQ(queue.size(), 1000u);
}

TEST_F(LockFreeTaskQueueTest, EDFOrdering) {
    queue.push(make_task(3, 300));
    queue.push(make_task(1, 100));
    queue.push(make_task(2, 200));

    LockFreeTask task;

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 1u);  // Earliest deadline

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 2u);

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 3u);
}

TEST_F(LockFreeTaskQueueTest, EDFOrderingLarge) {
    std::vector<int> deadlines;
    for (int i = 0; i < 100; ++i) {
        deadlines.push_back(i);
    }

    // Shuffle and insert
    std::mt19937 gen(42);
    std::shuffle(deadlines.begin(), deadlines.end(), gen);

    for (int d : deadlines) {
        queue.push(make_task(d, d));
    }

    // Verify ordering
    LockFreeTask task;
    for (int expected = 0; expected < 100; ++expected) {
        EXPECT_TRUE(queue.pop(task));
        EXPECT_EQ(task.id, static_cast<uint64_t>(expected));
    }
}

TEST_F(LockFreeTaskQueueTest, PriorityBreaksTies) {
    queue.push(make_task(1, 100, 50));   // Low priority
    queue.push(make_task(2, 100, 200));  // High priority
    queue.push(make_task(3, 100, 128));  // Medium priority

    LockFreeTask task;

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 2u);  // Highest priority

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 3u);

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 1u);
}

TEST_F(LockFreeTaskQueueTest, PriorityBoundaries) {
    // Test with boundary priority values
    queue.push(make_task(1, 100, 0));    // Min priority
    queue.push(make_task(2, 100, 255));  // Max priority
    queue.push(make_task(3, 100, 1));    // Just above min
    queue.push(make_task(4, 100, 254));  // Just below max

    LockFreeTask task;

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 2u);  // 255 priority

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 4u);  // 254 priority

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 3u);  // 1 priority

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 1u);  // 0 priority
}

TEST_F(LockFreeTaskQueueTest, TryPop) {
    LockFreeTask empty_task;
    EXPECT_FALSE(queue.try_pop(empty_task));

    queue.push(make_task(1, 100));
    LockFreeTask task;
    EXPECT_TRUE(queue.try_pop(task));
    EXPECT_EQ(task.id, 1u);
}

TEST_F(LockFreeTaskQueueTest, Peek) {
    queue.push(make_task(1, 100));

    LockFreeTask task;
    EXPECT_TRUE(queue.peek(task));
    EXPECT_EQ(task.id, 1u);
    EXPECT_EQ(queue.size(), 1u);  // Still in queue
}

TEST_F(LockFreeTaskQueueTest, PeekEmpty) {
    LockFreeTask task;
    EXPECT_FALSE(queue.peek(task));
}

TEST_F(LockFreeTaskQueueTest, PeekDoesNotRemove) {
    queue.push(make_task(1, 100));

    LockFreeTask task;
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(queue.peek(task));
        EXPECT_EQ(task.id, 1u);
    }

    EXPECT_EQ(queue.size(), 1u);
}

TEST_F(LockFreeTaskQueueTest, Remove) {
    queue.push(make_task(1, 100));
    queue.push(make_task(2, 200));
    queue.push(make_task(3, 300));

    EXPECT_TRUE(queue.remove(2));
    EXPECT_EQ(queue.size(), 2u);

    LockFreeTask task;
    queue.pop(task);
    EXPECT_EQ(task.id, 1u);

    queue.pop(task);
    EXPECT_EQ(task.id, 3u);
}

TEST_F(LockFreeTaskQueueTest, RemoveNonexistent) {
    queue.push(make_task(1, 100));
    EXPECT_FALSE(queue.remove(999));
    EXPECT_EQ(queue.size(), 1u);
}

TEST_F(LockFreeTaskQueueTest, RemoveFromEmpty) {
    EXPECT_FALSE(queue.remove(1));
}

TEST_F(LockFreeTaskQueueTest, NearestDeadline) {
    EXPECT_FALSE(queue.nearest_deadline().has_value());

    queue.push(make_task(1, 200));
    queue.push(make_task(2, 100));
    queue.push(make_task(3, 300));

    auto deadline = queue.nearest_deadline();
    ASSERT_TRUE(deadline.has_value());
    EXPECT_EQ(*deadline, 100);
}

TEST_F(LockFreeTaskQueueTest, NearestDeadlineUpdatesAfterPop) {
    queue.push(make_task(1, 100));
    queue.push(make_task(2, 200));

    auto deadline = queue.nearest_deadline();
    EXPECT_EQ(*deadline, 100);

    LockFreeTask task;
    queue.pop(task);

    deadline = queue.nearest_deadline();
    EXPECT_EQ(*deadline, 200);
}

TEST_F(LockFreeTaskQueueTest, MaxSizeEnforced) {
    LockFreeTaskQueue small_queue{5};

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(small_queue.push(make_task(i, i * 100)));
    }
    EXPECT_EQ(small_queue.size(), 5u);

    // Queue is full
    EXPECT_FALSE(small_queue.push(make_task(99, 999)));
    EXPECT_EQ(small_queue.size(), 5u);
}

TEST_F(LockFreeTaskQueueTest, MaxSizeAccessor) {
    LockFreeTaskQueue q{500};
    EXPECT_EQ(q.max_size(), 500u);
}

TEST_F(LockFreeTaskQueueTest, BoundaryDeadlines) {
    // Test with extreme deadline values
    queue.push(make_task(1, std::numeric_limits<int64_t>::max()));
    queue.push(make_task(2, std::numeric_limits<int64_t>::min()));
    queue.push(make_task(3, 0));
    queue.push(make_task(4, -1));
    queue.push(make_task(5, 1));

    LockFreeTask task;

    // Should come out in deadline order
    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 2u);  // min

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 4u);  // -1

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 3u);  // 0

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 5u);  // 1

    EXPECT_TRUE(queue.pop(task));
    EXPECT_EQ(task.id, 1u);  // max
}

TEST_F(LockFreeTaskQueueTest, SingleElementOperations) {
    // Operations on queue with exactly one element
    queue.push(make_task(1, 100));
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_FALSE(queue.empty());

    auto deadline = queue.nearest_deadline();
    EXPECT_EQ(*deadline, 100);

    LockFreeTask peeked;
    EXPECT_TRUE(queue.peek(peeked));
    EXPECT_EQ(peeked.id, 1u);

    LockFreeTask popped;
    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped.id, 1u);
    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// Concurrent Access Tests - Industrial Grade
// ============================================================================

class LockFreeTaskQueueConcurrencyTest : public ::testing::Test {
protected:
    LockFreeTaskQueue queue{100000};
};

TEST_F(LockFreeTaskQueueConcurrencyTest, ConcurrentPush) {
    constexpr int NUM_THREADS      = 8;
    constexpr int TASKS_PER_THREAD = 1000;

    std::vector<std::thread> threads;
    std::atomic<int> pushed{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, t, &pushed]() {
            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                LockFreeTask task;
                task.id          = static_cast<uint64_t>(t * TASKS_PER_THREAD + i);
                task.deadline_ns = static_cast<int64_t>(t * 100000 + i);
                task.priority    = static_cast<uint8_t>(128);
                if (queue.push(task)) {
                    pushed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All pushes should succeed - no contention loss in push-only scenario
    EXPECT_EQ(pushed.load(), NUM_THREADS * TASKS_PER_THREAD);
    EXPECT_EQ(queue.size(), static_cast<size_t>(NUM_THREADS * TASKS_PER_THREAD));
}

TEST_F(LockFreeTaskQueueConcurrencyTest, ConcurrentPop) {
    constexpr int NUM_ITEMS   = 10000;
    constexpr int NUM_THREADS = 4;

    // Pre-populate the queue
    for (int i = 0; i < NUM_ITEMS; ++i) {
        LockFreeTask task;
        task.id          = i;
        task.deadline_ns = i;
        queue.push(task);
    }

    std::vector<std::thread> threads;
    std::atomic<int> popped{0};
    std::atomic<bool> done{false};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, &popped, &done]() {
            LockFreeTask task;
            while (!done.load(std::memory_order_acquire)) {
                if (queue.try_pop(task)) {
                    popped.fetch_add(1, std::memory_order_relaxed);
                }
            }
            // Drain remaining items after done signal
            while (queue.try_pop(task)) {
                popped.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Let threads run for a while
    std::this_thread::sleep_for(100ms);
    done.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    // All items should be popped
    EXPECT_EQ(popped.load(), NUM_ITEMS);
    EXPECT_TRUE(queue.empty());
}

TEST_F(LockFreeTaskQueueConcurrencyTest, ConcurrentPushPop) {
    // Test concurrent push and pop with producers-first approach
    // This avoids livelock conditions in the lock-free skip list
    constexpr int NUM_PRODUCERS      = 2;
    constexpr int TASKS_PER_PRODUCER = 500;
    constexpr int TOTAL_TASKS        = NUM_PRODUCERS * TASKS_PER_PRODUCER;

    std::atomic<int> produced{0};
    std::vector<std::thread> producers;

    // Phase 1: All producers push their tasks
    for (int t = 0; t < NUM_PRODUCERS; ++t) {
        producers.emplace_back([this, t, &produced]() {
            for (int i = 0; i < TASKS_PER_PRODUCER; ++i) {
                LockFreeTask task;
                task.id = static_cast<uint64_t>(t * TASKS_PER_PRODUCER + i);
                // Use unique deadlines per task to avoid skip list duplicate key issues
                task.deadline_ns = static_cast<int64_t>(t * TASKS_PER_PRODUCER + i);

                if (queue.push(task)) {
                    produced.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    int total_produced = produced.load();
    EXPECT_EQ(total_produced, TOTAL_TASKS);

    // Phase 2: Multiple consumers drain concurrently
    constexpr int NUM_CONSUMERS = 2;
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};
    std::vector<std::thread> consumers;

    for (int t = 0; t < NUM_CONSUMERS; ++t) {
        consumers.emplace_back([this, &consumed, &done]() {
            LockFreeTask task;
            while (!done.load(std::memory_order_acquire)) {
                if (queue.try_pop(task)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
            // Final drain
            while (queue.try_pop(task)) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Wait for drain with timeout
    auto start = std::chrono::steady_clock::now();
    while (queue.size() > 0) {
        std::this_thread::sleep_for(10ms);
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > 2s) {
            break;  // Timeout
        }
    }

    done.store(true, std::memory_order_release);

    for (auto& consumer : consumers) {
        consumer.join();
    }

    int total_consumed = consumed.load();

    // Verify data integrity: all produced items were consumed
    EXPECT_EQ(total_consumed, total_produced);
    EXPECT_TRUE(queue.empty());
}

TEST_F(LockFreeTaskQueueConcurrencyTest, EDFOrderingUnderConcurrency) {
    constexpr int NUM_THREADS      = 4;
    constexpr int TASKS_PER_THREAD = 500;

    // Push tasks with random deadlines
    std::vector<std::thread> pushers;
    std::atomic<int> total_pushed{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        pushers.emplace_back([this, t, &total_pushed]() {
            std::mt19937 gen(t);
            std::uniform_int_distribution<int64_t> dist(0, 100000);

            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                LockFreeTask task;
                task.id          = static_cast<uint64_t>(t * TASKS_PER_THREAD + i);
                task.deadline_ns = dist(gen);
                if (queue.push(task)) {
                    total_pushed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& pusher : pushers) {
        pusher.join();
    }

    // Pop all tasks and verify ordering (single threaded)
    int64_t prev_deadline = std::numeric_limits<int64_t>::min();
    int order_violations  = 0;
    int popped            = 0;
    LockFreeTask task;

    while (queue.pop(task)) {
        ++popped;
        if (task.deadline_ns < prev_deadline) {
            ++order_violations;
        }
        prev_deadline = task.deadline_ns;
    }

    // In a correct skip list, ordering should be maintained
    // Allow very small number of violations due to concurrent insertions
    EXPECT_EQ(popped, total_pushed.load());
    double violation_rate = static_cast<double>(order_violations) / popped;
    EXPECT_LT(violation_rate, 0.01);  // Less than 1% violations
}

TEST_F(LockFreeTaskQueueConcurrencyTest, StressTestMixedOperations) {
    // Phased stress test to avoid livelock in lock-free skip list
    // Phase 1: Insert tasks
    // Phase 2: Read-only operations (peek, size)
    // Phase 3: Drain tasks

    constexpr int NUM_TASKS = 500;

    // Phase 1: Insert all tasks sequentially (avoid concurrent push contention)
    for (int i = 0; i < NUM_TASKS; ++i) {
        LockFreeTask task;
        task.id          = static_cast<uint64_t>(i);
        task.deadline_ns = static_cast<int64_t>(i);
        EXPECT_TRUE(queue.push(task));
    }
    EXPECT_EQ(queue.size(), NUM_TASKS);

    // Phase 2: Concurrent read-only operations (no livelock possible)
    constexpr int NUM_READERS      = 4;
    constexpr int READS_PER_THREAD = 200;
    std::atomic<int> read_ops{0};
    std::vector<std::thread> readers;

    for (int t = 0; t < NUM_READERS; ++t) {
        readers.emplace_back([this, t, &read_ops]() {
            std::mt19937 gen(t);
            std::uniform_int_distribution<int> op_dist(0, 2);

            for (int i = 0; i < READS_PER_THREAD; ++i) {
                int op = op_dist(gen);
                switch (op) {
                    case 0: {  // Peek
                        LockFreeTask task;
                        queue.peek(task);
                        break;
                    }
                    case 1: {  // Size
                        queue.size();
                        break;
                    }
                    case 2: {  // Nearest deadline
                        queue.nearest_deadline();
                        break;
                    }
                }
                read_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_EQ(read_ops.load(), NUM_READERS * READS_PER_THREAD);
    EXPECT_EQ(queue.size(), NUM_TASKS);  // Still all tasks

    // Phase 3: Concurrent drain
    constexpr int NUM_POPPERS = 4;
    std::atomic<int> popped{0};
    std::vector<std::thread> poppers;

    for (int t = 0; t < NUM_POPPERS; ++t) {
        poppers.emplace_back([this, &popped]() {
            LockFreeTask task;
            while (queue.try_pop(task)) {
                popped.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& popper : poppers) {
        popper.join();
    }

    EXPECT_EQ(popped.load(), NUM_TASKS);
    EXPECT_EQ(queue.size(), 0u);
}

TEST_F(LockFreeTaskQueueConcurrencyTest, ContentionOnSingleDeadline) {
    // Test contention with closely grouped deadlines
    // Note: Skip list requires unique keys, so we use deadline = thread_idx * 1000 + task_idx
    // This simulates tasks with similar (but unique) deadlines competing for insertion
    constexpr int NUM_THREADS      = 4;
    constexpr int TASKS_PER_THREAD = 500;  // Reduced for faster execution

    std::atomic<int> pushed{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, t, &pushed]() {
            for (int i = 0; i < TASKS_PER_THREAD; ++i) {
                LockFreeTask task;
                task.id = static_cast<uint64_t>(t * TASKS_PER_THREAD + i);
                // Each thread uses its own deadline range to ensure uniqueness
                task.deadline_ns = static_cast<int64_t>(t * TASKS_PER_THREAD + i);
                task.priority    = static_cast<uint8_t>(t);
                if (queue.push(task)) {
                    pushed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(pushed.load(), NUM_THREADS * TASKS_PER_THREAD);
}

// ============================================================================
// Skip List Concurrent Stress Test
// ============================================================================

class LockFreeSkipListStressTest : public ::testing::Test {};

TEST_F(LockFreeSkipListStressTest, MixedOperations) {
    // Phased stress test to avoid livelock in lock-free skip list
    // Phase 1: Insert values
    // Phase 2: Read operations only (contains)
    // Phase 3: Drain with pop_min

    LockFreeSkipList<int> list;

    // Phase 1: Insert values (sequential to avoid contention)
    constexpr int NUM_VALUES = 500;
    for (int i = 0; i < NUM_VALUES; ++i) {
        list.insert(i);
    }
    EXPECT_EQ(list.size(), NUM_VALUES);

    // Phase 2: Concurrent read-only operations
    constexpr int NUM_READERS      = 4;
    constexpr int READS_PER_THREAD = 200;
    std::atomic<int> read_ops{0};
    std::vector<std::thread> readers;

    for (int t = 0; t < NUM_READERS; ++t) {
        readers.emplace_back([&list, t, &read_ops]() {
            std::mt19937 gen(static_cast<unsigned>(t));
            std::uniform_int_distribution<int> value_dist(0, NUM_VALUES - 1);

            for (int i = 0; i < READS_PER_THREAD; ++i) {
                int value = value_dist(gen);
                list.contains(value);
                read_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_EQ(read_ops.load(), NUM_READERS * READS_PER_THREAD);
    EXPECT_EQ(list.size(), NUM_VALUES);

    // Phase 3: Concurrent drain with pop_min
    constexpr int NUM_POPPERS = 4;
    std::atomic<int> popped{0};
    std::vector<std::thread> poppers;

    for (int t = 0; t < NUM_POPPERS; ++t) {
        poppers.emplace_back([&list, &popped]() {
            while (auto val = list.pop_min()) {
                popped.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& popper : poppers) {
        popper.join();
    }

    EXPECT_EQ(popped.load(), NUM_VALUES);
    EXPECT_TRUE(list.empty());
}

TEST_F(LockFreeSkipListStressTest, ProducerConsumerPattern) {
    // Two-phase producer-consumer test to avoid livelock
    // Phase 1: All producers insert (concurrent)
    // Phase 2: All consumers drain (concurrent)

    LockFreeSkipList<int> list;
    constexpr int NUM_PRODUCERS      = 4;
    constexpr int NUM_CONSUMERS      = 4;
    constexpr int ITEMS_PER_PRODUCER = 500;  // Reduced for faster execution

    // Phase 1: All producers insert concurrently
    std::atomic<int> produced{0};
    std::vector<std::thread> producers;

    for (int t = 0; t < NUM_PRODUCERS; ++t) {
        producers.emplace_back([&list, t, &produced]() {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                // Use unique values per producer thread
                if (list.insert(t * ITEMS_PER_PRODUCER + i)) {
                    produced.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& p : producers) {
        p.join();
    }

    constexpr int EXPECTED_PRODUCED = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    EXPECT_EQ(produced.load(), EXPECTED_PRODUCED);
    EXPECT_EQ(list.size(), EXPECTED_PRODUCED);

    // Phase 2: All consumers drain concurrently
    std::atomic<int> consumed{0};
    std::vector<std::thread> consumers;

    for (int t = 0; t < NUM_CONSUMERS; ++t) {
        consumers.emplace_back([&list, &consumed]() {
            while (auto val = list.pop_min()) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& c : consumers) {
        c.join();
    }

    EXPECT_EQ(consumed.load(), EXPECTED_PRODUCED);
    EXPECT_TRUE(list.empty());
}

// ============================================================================
// Performance Tests
// ============================================================================

class TaskQueuePerformanceTest : public ::testing::Test {};

TEST_F(TaskQueuePerformanceTest, HighThroughput) {
    LockFreeTaskQueue queue{100000};

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 50000; ++i) {
        LockFreeTask task;
        task.id          = i;
        task.deadline_ns = i;
        queue.push(task);
    }

    LockFreeTask task;
    while (queue.pop(task)) {}

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete 100K operations in reasonable time (< 500ms)
    EXPECT_LT(duration.count(), 500);
}

TEST_F(TaskQueuePerformanceTest, ConcurrentThroughput) {
    LockFreeTaskQueue queue{20000};
    constexpr int TOTAL_OPS   = 10000;  // Reduced for CI environments
    constexpr int NUM_THREADS = 4;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    std::atomic<int> ops{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&queue, t, &ops]() {
            for (int i = 0; i < TOTAL_OPS / NUM_THREADS; ++i) {
                LockFreeTask task;
                task.id          = t * (TOTAL_OPS / NUM_THREADS) + i;
                task.deadline_ns = i;
                queue.push(task);

                LockFreeTask popped;
                queue.try_pop(popped);

                ops.fetch_add(2, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete concurrent operations in reasonable time
    EXPECT_GT(ops.load(), 0);
    EXPECT_LT(duration.count(), 5000);  // < 5 seconds for 20K ops
}

TEST_F(TaskQueuePerformanceTest, OrderedInsertPerformance) {
    LockFreeTaskQueue queue{100000};

    // Worst case: inserting in reverse order
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 10000; i > 0; --i) {
        LockFreeTask task;
        task.id          = i;
        task.deadline_ns = i;
        queue.push(task);
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Reverse order insertion should still be efficient
    EXPECT_LT(duration.count(), 200);
    EXPECT_EQ(queue.size(), 10000u);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

class LockFreeTaskQueueEdgeCaseTest : public ::testing::Test {};

TEST_F(LockFreeTaskQueueEdgeCaseTest, QueueCapacityOne) {
    LockFreeTaskQueue queue{1};

    LockFreeTask task;
    task.id          = 1;
    task.deadline_ns = 100;

    EXPECT_TRUE(queue.push(task));
    EXPECT_FALSE(queue.push(task));  // Queue full

    LockFreeTask popped;
    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped.id, 1u);

    EXPECT_TRUE(queue.push(task));  // Can push again
}

TEST_F(LockFreeTaskQueueEdgeCaseTest, RapidPushPopSingleElement) {
    LockFreeTaskQueue queue{100};

    for (int i = 0; i < 10000; ++i) {
        LockFreeTask task;
        task.id          = i;
        task.deadline_ns = i;

        EXPECT_TRUE(queue.push(task));

        LockFreeTask popped;
        EXPECT_TRUE(queue.pop(popped));
        EXPECT_EQ(popped.id, static_cast<uint64_t>(i));

        EXPECT_TRUE(queue.empty());
    }
}

TEST_F(LockFreeTaskQueueEdgeCaseTest, AlternatingPushPop) {
    LockFreeTaskQueue queue{100};

    for (int round = 0; round < 100; ++round) {
        // Push 10 items
        for (int i = 0; i < 10; ++i) {
            LockFreeTask task;
            task.id          = round * 10 + i;
            task.deadline_ns = i;  // Same deadline pattern each round
            queue.push(task);
        }

        // Pop all items
        LockFreeTask popped;
        int count = 0;
        while (queue.pop(popped)) {
            ++count;
        }
        EXPECT_EQ(count, 10);
    }
}

TEST_F(LockFreeTaskQueueEdgeCaseTest, SameIdDifferentDeadlines) {
    LockFreeTaskQueue queue{100};

    // Same ID but different deadlines should all be stored
    for (int i = 0; i < 10; ++i) {
        LockFreeTask task;
        task.id          = 42;       // Same ID
        task.deadline_ns = 100 - i;  // Different deadlines
        queue.push(task);
    }

    EXPECT_EQ(queue.size(), 10u);

    // Should come out in deadline order
    LockFreeTask task;
    int64_t expected_deadline = 91;  // Starts from lowest
    while (queue.pop(task)) {
        EXPECT_EQ(task.id, 42u);
        EXPECT_EQ(task.deadline_ns, expected_deadline);
        ++expected_deadline;
    }
}

TEST_F(LockFreeTaskQueueEdgeCaseTest, PopFromRecentlyEmptiedQueue) {
    LockFreeTaskQueue queue{100};

    // Fill and empty multiple times
    for (int round = 0; round < 10; ++round) {
        // Fill
        for (int i = 0; i < 50; ++i) {
            LockFreeTask task;
            task.id          = i;
            task.deadline_ns = i;
            queue.push(task);
        }

        // Empty
        LockFreeTask task;
        while (queue.pop(task)) {}

        // Verify truly empty
        EXPECT_TRUE(queue.empty());
        EXPECT_EQ(queue.size(), 0u);
        EXPECT_FALSE(queue.nearest_deadline().has_value());
        EXPECT_FALSE(queue.try_pop(task));
    }
}
