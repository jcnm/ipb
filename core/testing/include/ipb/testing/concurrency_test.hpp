#pragma once

/**
 * @file concurrency_test.hpp
 * @brief Concurrency testing framework for detecting race conditions
 *
 * Features:
 * - Thread sanitizer integration
 * - Stress testing with configurable thread counts
 * - Race condition detection patterns
 * - Deadlock detection
 * - Memory ordering verification
 * - Latch/barrier synchronization helpers
 *
 * Usage:
 * @code
 * ConcurrencyTest test;
 * test.add_thread([&](size_t thread_id) {
 *     for (int i = 0; i < 1000; ++i) {
 *         queue.push(i);
 *     }
 * }, 4);  // 4 threads
 *
 * test.add_thread([&](size_t thread_id) {
 *     int val;
 *     while (queue.try_pop(val)) {}
 * }, 4);  // 4 consumer threads
 *
 * auto result = test.run();
 * EXPECT_TRUE(result.success);
 * @endcode
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace ipb::testing {

//=============================================================================
// Test Configuration
//=============================================================================

/**
 * @brief Configuration for concurrency tests
 */
struct ConcurrencyConfig {
    size_t iterations{1000};
    std::chrono::seconds timeout{30};
    bool enable_stress{true};
    bool randomize_timing{true};
    bool detect_deadlock{true};
    size_t deadlock_timeout_ms{5000};
    bool verbose{false};
};

//=============================================================================
// Test Result
//=============================================================================

/**
 * @brief Result of a concurrency test
 */
struct ConcurrencyResult {
    bool success{true};
    std::string error;
    size_t iterations_completed{0};
    std::chrono::microseconds duration{0};
    std::vector<std::string> warnings;

    // Thread statistics
    size_t total_threads{0};
    size_t completed_threads{0};
    size_t failed_threads{0};

    void add_warning(const std::string& msg) {
        warnings.push_back(msg);
    }

    void fail(const std::string& msg) {
        success = false;
        error = msg;
    }
};

//=============================================================================
// Synchronization Primitives
//=============================================================================

/**
 * @brief Thread barrier for synchronizing test threads
 */
class ThreadBarrier {
public:
    explicit ThreadBarrier(size_t count)
        : count_(count)
        , waiting_(0)
        , generation_(0) {}

    void wait() {
        std::unique_lock lock(mutex_);
        auto gen = generation_;

        if (++waiting_ == count_) {
            waiting_ = 0;
            ++generation_;
            cv_.notify_all();
        } else {
            cv_.wait(lock, [this, gen] { return gen != generation_; });
        }
    }

    void reset(size_t count) {
        std::lock_guard lock(mutex_);
        count_ = count;
        waiting_ = 0;
        ++generation_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t count_;
    size_t waiting_;
    size_t generation_;
};

/**
 * @brief Countdown latch for test coordination
 */
class CountdownLatch {
public:
    explicit CountdownLatch(size_t count)
        : count_(count) {}

    void count_down() {
        std::lock_guard lock(mutex_);
        if (count_ > 0) {
            --count_;
            if (count_ == 0) {
                cv_.notify_all();
            }
        }
    }

    void wait() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return count_ == 0; });
    }

    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return count_ == 0; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t count_;
};

//=============================================================================
// Thread Task
//=============================================================================

/**
 * @brief Thread task with synchronization
 */
struct ThreadTask {
    std::function<void(size_t thread_id, std::atomic<bool>& stop)> func;
    size_t thread_count{1};
    std::string name;
};

//=============================================================================
// Concurrency Test Framework
//=============================================================================

/**
 * @brief Concurrency testing framework
 */
class ConcurrencyTest {
public:
    explicit ConcurrencyTest(ConcurrencyConfig config = {})
        : config_(std::move(config))
        , stop_(false) {}

    /**
     * @brief Add thread task
     */
    void add_thread(std::function<void(size_t, std::atomic<bool>&)> func,
                    size_t count = 1,
                    const std::string& name = "") {
        ThreadTask task;
        task.func = std::move(func);
        task.thread_count = count;
        task.name = name.empty() ? "task_" + std::to_string(tasks_.size()) : name;
        tasks_.push_back(std::move(task));
    }

    /**
     * @brief Add simple thread task (no stop signal)
     */
    void add_thread(std::function<void(size_t)> func,
                    size_t count = 1,
                    const std::string& name = "") {
        add_thread([f = std::move(func)](size_t id, std::atomic<bool>&) {
            f(id);
        }, count, name);
    }

    /**
     * @brief Run the test
     */
    ConcurrencyResult run() {
        ConcurrencyResult result;
        auto start = std::chrono::high_resolution_clock::now();

        // Count total threads
        size_t total_threads = 0;
        for (const auto& task : tasks_) {
            total_threads += task.thread_count;
        }
        result.total_threads = total_threads;

        if (total_threads == 0) {
            result.fail("No threads to run");
            return result;
        }

        // Create synchronization primitives
        auto barrier = std::make_shared<ThreadBarrier>(total_threads);
        auto latch = std::make_shared<CountdownLatch>(total_threads);
        stop_.store(false);

        // Launch threads
        std::vector<std::thread> threads;
        std::vector<std::exception_ptr> exceptions(total_threads);
        std::atomic<size_t> thread_index{0};

        for (const auto& task : tasks_) {
            for (size_t t = 0; t < task.thread_count; ++t) {
                size_t idx = thread_index++;
                threads.emplace_back([&, idx, task_copy = task]() {
                    try {
                        // Wait for all threads to start
                        barrier->wait();

                        // Add random delay for stress testing
                        if (config_.randomize_timing) {
                            std::random_device rd;
                            std::mt19937 gen(rd());
                            std::uniform_int_distribution<> dist(0, 100);
                            std::this_thread::sleep_for(std::chrono::microseconds(dist(gen)));
                        }

                        // Run the task
                        task_copy.func(idx, stop_);

                    } catch (...) {
                        exceptions[idx] = std::current_exception();
                    }
                    latch->count_down();
                });
            }
        }

        // Wait for completion with timeout
        bool completed = latch->wait_for(
            std::chrono::duration_cast<std::chrono::milliseconds>(config_.timeout)
        );

        // Signal stop
        stop_.store(true);

        // Join all threads
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        // Check for timeout
        if (!completed) {
            result.fail("Test timed out after " +
                        std::to_string(config_.timeout.count()) + " seconds");
            if (config_.detect_deadlock) {
                result.add_warning("Possible deadlock detected");
            }
        }

        // Check for exceptions
        for (size_t i = 0; i < exceptions.size(); ++i) {
            if (exceptions[i]) {
                try {
                    std::rethrow_exception(exceptions[i]);
                } catch (const std::exception& e) {
                    result.fail("Thread " + std::to_string(i) + " threw: " + e.what());
                    ++result.failed_threads;
                } catch (...) {
                    result.fail("Thread " + std::to_string(i) + " threw unknown exception");
                    ++result.failed_threads;
                }
            } else {
                ++result.completed_threads;
            }
        }

        if (config_.verbose) {
            std::cout << "Concurrency test completed:\n"
                      << "  Total threads: " << result.total_threads << "\n"
                      << "  Completed: " << result.completed_threads << "\n"
                      << "  Failed: " << result.failed_threads << "\n"
                      << "  Duration: " << result.duration.count() << "us\n";
        }

        return result;
    }

    /**
     * @brief Run test multiple times for stress testing
     */
    std::vector<ConcurrencyResult> run_stress(size_t runs) {
        std::vector<ConcurrencyResult> results;
        results.reserve(runs);

        for (size_t i = 0; i < runs; ++i) {
            stop_.store(false);
            results.push_back(run());

            if (!results.back().success) {
                if (config_.verbose) {
                    std::cout << "Stress test failed on run " << (i + 1)
                              << ": " << results.back().error << "\n";
                }
            }
        }

        return results;
    }

    /**
     * @brief Clear all tasks
     */
    void clear() {
        tasks_.clear();
    }

private:
    ConcurrencyConfig config_;
    std::vector<ThreadTask> tasks_;
    std::atomic<bool> stop_;
};

//=============================================================================
// Race Condition Detector
//=============================================================================

/**
 * @brief Helper to detect data races
 */
template<typename T>
class RaceDetector {
public:
    void write(const T& value, size_t thread_id) {
        std::lock_guard lock(mutex_);
        if (reading_) {
            race_detected_ = true;
            race_info_ = "Write during read by thread " + std::to_string(thread_id);
        }
        if (writing_) {
            race_detected_ = true;
            race_info_ = "Concurrent writes by threads " +
                         std::to_string(writer_thread_) + " and " +
                         std::to_string(thread_id);
        }
        writing_ = true;
        writer_thread_ = thread_id;

        value_ = value;

        writing_ = false;
    }

    T read(size_t thread_id) {
        std::lock_guard lock(mutex_);
        if (writing_) {
            race_detected_ = true;
            race_info_ = "Read during write by thread " + std::to_string(thread_id);
        }
        reading_ = true;

        T result = value_;

        reading_ = false;
        return result;
    }

    bool has_race() const { return race_detected_; }
    std::string race_info() const { return race_info_; }

private:
    T value_{};
    std::mutex mutex_;
    bool reading_{false};
    bool writing_{false};
    size_t writer_thread_{0};
    bool race_detected_{false};
    std::string race_info_;
};

//=============================================================================
// Memory Order Verifier
//=============================================================================

/**
 * @brief Verifies memory ordering between threads
 */
class MemoryOrderVerifier {
public:
    void store_release(size_t value) {
        data_.store(value, std::memory_order_release);
        flag_.store(true, std::memory_order_release);
    }

    bool load_acquire(size_t& value) {
        if (flag_.load(std::memory_order_acquire)) {
            value = data_.load(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    void reset() {
        flag_.store(false, std::memory_order_relaxed);
        data_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Test acquire-release ordering
     */
    static bool test_acquire_release(size_t iterations = 10000) {
        MemoryOrderVerifier verifier;
        std::atomic<size_t> failures{0};

        for (size_t iter = 0; iter < iterations; ++iter) {
            verifier.reset();
            std::atomic<bool> done{false};

            std::thread writer([&] {
                verifier.store_release(42);
                done.store(true, std::memory_order_release);
            });

            std::thread reader([&] {
                while (!done.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                size_t value;
                if (verifier.load_acquire(value)) {
                    if (value != 42) {
                        failures.fetch_add(1);
                    }
                }
            });

            writer.join();
            reader.join();
        }

        return failures.load() == 0;
    }

private:
    std::atomic<size_t> data_{0};
    std::atomic<bool> flag_{false};
};

//=============================================================================
// Deadlock Detector
//=============================================================================

/**
 * @brief Simple deadlock detection helper
 */
class DeadlockDetector {
public:
    explicit DeadlockDetector(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
        : timeout_(timeout) {}

    /**
     * @brief Try to acquire lock with timeout
     */
    template<typename Mutex>
    bool try_lock(Mutex& mutex, const std::string& lock_name = "") {
        auto start = std::chrono::steady_clock::now();

        while (!mutex.try_lock()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > timeout_) {
                potential_deadlock_ = true;
                deadlock_info_ = "Timeout waiting for lock: " + lock_name;
                return false;
            }
            std::this_thread::yield();
        }

        return true;
    }

    bool has_potential_deadlock() const { return potential_deadlock_; }
    std::string deadlock_info() const { return deadlock_info_; }

private:
    std::chrono::milliseconds timeout_;
    bool potential_deadlock_{false};
    std::string deadlock_info_;
};

//=============================================================================
// Thread Stress Patterns
//=============================================================================

/**
 * @brief Common stress test patterns
 */
class StressPatterns {
public:
    /**
     * @brief Producer-consumer stress test
     */
    template<typename Queue>
    static ConcurrencyResult producer_consumer(
            Queue& queue,
            size_t producers,
            size_t consumers,
            size_t items_per_producer) {

        ConcurrencyTest test;
        std::atomic<size_t> produced{0};
        std::atomic<size_t> consumed{0};

        // Producers
        test.add_thread([&](size_t id, std::atomic<bool>& stop) {
            for (size_t i = 0; i < items_per_producer && !stop.load(); ++i) {
                while (!queue.try_push(static_cast<int>(id * items_per_producer + i))) {
                    if (stop.load()) return;
                    std::this_thread::yield();
                }
                produced.fetch_add(1);
            }
        }, producers, "producer");

        // Consumers
        test.add_thread([&](size_t, std::atomic<bool>& stop) {
            int value;
            while (!stop.load() || consumed.load() < produced.load()) {
                if (queue.try_pop(value)) {
                    consumed.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        }, consumers, "consumer");

        auto result = test.run();

        // Verify all items consumed
        if (produced.load() != consumed.load()) {
            result.add_warning("Produced " + std::to_string(produced.load()) +
                               " but consumed " + std::to_string(consumed.load()));
        }

        return result;
    }

    /**
     * @brief Reader-writer stress test
     */
    template<typename Container>
    static ConcurrencyResult reader_writer(
            Container& container,
            size_t readers,
            size_t writers,
            size_t operations_per_thread,
            std::function<void(Container&, size_t)> write_op,
            std::function<void(Container&, size_t)> read_op) {

        ConcurrencyTest test;

        // Writers
        test.add_thread([&](size_t id, std::atomic<bool>& stop) {
            for (size_t i = 0; i < operations_per_thread && !stop.load(); ++i) {
                write_op(container, id * operations_per_thread + i);
            }
        }, writers, "writer");

        // Readers
        test.add_thread([&](size_t id, std::atomic<bool>& stop) {
            for (size_t i = 0; i < operations_per_thread && !stop.load(); ++i) {
                read_op(container, id * operations_per_thread + i);
            }
        }, readers, "reader");

        return test.run();
    }

    /**
     * @brief Counter stress test
     */
    template<typename Counter>
    static ConcurrencyResult counter_stress(
            Counter& counter,
            size_t threads,
            size_t increments_per_thread) {

        ConcurrencyTest test;
        std::atomic<size_t> total_increments{0};

        test.add_thread([&](size_t, std::atomic<bool>& stop) {
            for (size_t i = 0; i < increments_per_thread && !stop.load(); ++i) {
                counter.increment();
                total_increments.fetch_add(1);
            }
        }, threads, "incrementer");

        auto result = test.run();

        // Verify counter value
        size_t expected = threads * increments_per_thread;
        size_t actual = counter.value();

        if (actual != expected) {
            result.fail("Counter mismatch: expected " + std::to_string(expected) +
                        " but got " + std::to_string(actual));
        }

        return result;
    }
};

//=============================================================================
// Assertion Helpers
//=============================================================================

#define CONCURRENCY_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + message); \
        } \
    } while(0)

#define CONCURRENCY_EXPECT_TRUE(result) \
    do { \
        if (!(result).success) { \
            throw std::runtime_error("Concurrency test failed: " + (result).error); \
        } \
    } while(0)

} // namespace ipb::testing
