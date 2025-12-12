/**
 * @file run_benchmarks.cpp
 * @brief Performance benchmarks for IPB core components
 *
 * Benchmarks for:
 * - Memory pool allocation/deallocation
 * - Lock-free queue operations
 * - Rate limiter throughput
 * - Backpressure controller
 * - Cache-optimized data structures
 */

#include "performance_benchmarks.hpp"
#include <ipb/common/memory_pool.hpp>
#include <ipb/common/lockfree_queue.hpp>
#include <ipb/common/rate_limiter.hpp>
#include <ipb/common/backpressure.hpp>
#include <ipb/common/cache_optimized.hpp>

#include <cstring>
#include <random>
#include <thread>

using namespace ipb::common;
using namespace ipb::common::benchmark;

namespace {

// Test data structures
struct TestData {
    uint64_t id;
    double value;
    char payload[48];  // Total 64 bytes
};

// Benchmark configurations
constexpr size_t ITERATIONS = 100000;
constexpr size_t WARMUP = 1000;

/**
 * @brief Memory Pool Benchmarks
 */
void run_memory_pool_benchmarks() {
    BenchmarkSuite suite("Memory Pool");

    // Pool for TestData
    ObjectPool<TestData, 256> pool(1024);

    // Benchmark: Pool allocation
    TestData* allocated = nullptr;
    suite.add_benchmark(
        "pool_allocate",
        [&]() {
            allocated = pool.allocate();
            DoNotOptimize(allocated);
        },
        [&]() { /* setup */ },
        [&]() {
            if (allocated) {
                pool.deallocate(allocated);
                allocated = nullptr;
            }
        },
        SLOSpec{.name = "pool_alloc", .p50_ns = 100, .p99_ns = 1000}
    );

    // Benchmark: Pool deallocation
    suite.add_benchmark(
        "pool_deallocate",
        [&]() {
            pool.deallocate(allocated);
            allocated = nullptr;
        },
        [&]() {
            allocated = pool.allocate();
        },
        [&]() { /* teardown */ },
        SLOSpec{.name = "pool_dealloc", .p50_ns = 100, .p99_ns = 1000}
    );

    // Benchmark: Allocate + deallocate cycle
    suite.add_benchmark(
        "pool_alloc_dealloc_cycle",
        [&]() {
            auto* ptr = pool.allocate();
            DoNotOptimize(ptr);
            pool.deallocate(ptr);
        },
        SLOSpec{.name = "pool_cycle", .p50_ns = 200, .p99_ns = 2000}
    );

    // Benchmark: Heap allocation for comparison
    suite.add_benchmark(
        "heap_new_delete",
        [&]() {
            auto* ptr = new TestData();
            DoNotOptimize(ptr);
            delete ptr;
        },
        SLOSpec{.name = "heap", .p50_ns = 500, .p99_ns = 5000}
    );

    // Run benchmarks
    BenchmarkConfig config;
    config.iterations = ITERATIONS;
    config.warmup_iterations = WARMUP;

    suite.run(config);
    suite.print_results();

    std::cout << "\nPool Stats:\n";
    std::cout << "  Hit rate: " << pool.stats().hit_rate() << "%\n";
    std::cout << "  Capacity: " << pool.capacity() << "\n";
}

/**
 * @brief Lock-free Queue Benchmarks
 */
void run_queue_benchmarks() {
    BenchmarkSuite suite("Lock-free Queues");

    // SPSC Queue
    SPSCQueue<uint64_t, 4096> spsc_queue;
    uint64_t value = 0;

    suite.add_benchmark(
        "spsc_enqueue",
        [&]() {
            spsc_queue.try_enqueue(value++);
        },
        [&]() { /* setup */ },
        [&]() {
            while (spsc_queue.try_dequeue()) {}
            value = 0;
        },
        SLOSpec{.name = "spsc_enqueue", .p50_ns = 50, .p99_ns = 500}
    );

    suite.add_benchmark(
        "spsc_dequeue",
        [&]() {
            auto result = spsc_queue.try_dequeue();
            DoNotOptimize(result);
        },
        [&]() {
            for (size_t i = 0; i < 1000; ++i) {
                spsc_queue.try_enqueue(i);
            }
        },
        [&]() { /* teardown */ },
        SLOSpec{.name = "spsc_dequeue", .p50_ns = 50, .p99_ns = 500}
    );

    suite.add_benchmark(
        "spsc_enqueue_dequeue_cycle",
        [&]() {
            spsc_queue.try_enqueue(value++);
            auto result = spsc_queue.try_dequeue();
            DoNotOptimize(result);
        },
        SLOSpec{.name = "spsc_cycle", .p50_ns = 100, .p99_ns = 1000}
    );

    // MPMC Queue
    BoundedMPMCQueue<uint64_t> mpmc_queue(4096);

    suite.add_benchmark(
        "mpmc_enqueue",
        [&]() {
            mpmc_queue.try_enqueue(value++);
        },
        [&]() { /* setup */ },
        [&]() {
            while (mpmc_queue.try_dequeue()) {}
            value = 0;
        },
        SLOSpec{.name = "mpmc_enqueue", .p50_ns = 100, .p99_ns = 1000}
    );

    suite.add_benchmark(
        "mpmc_dequeue",
        [&]() {
            auto result = mpmc_queue.try_dequeue();
            DoNotOptimize(result);
        },
        [&]() {
            for (size_t i = 0; i < 1000; ++i) {
                mpmc_queue.try_enqueue(i);
            }
        },
        [&]() { /* teardown */ },
        SLOSpec{.name = "mpmc_dequeue", .p50_ns = 100, .p99_ns = 1000}
    );

    BenchmarkConfig config;
    config.iterations = ITERATIONS;
    config.warmup_iterations = WARMUP;

    suite.run(config);
    suite.print_results();
}

/**
 * @brief Rate Limiter Benchmarks
 */
void run_rate_limiter_benchmarks() {
    BenchmarkSuite suite("Rate Limiter");

    // Token bucket - high rate (should mostly allow)
    TokenBucket fast_bucket(RateLimitConfig{
        .rate_per_second = 1000000,  // 1M/s
        .burst_size = 10000
    });

    suite.add_benchmark(
        "token_bucket_try_acquire_allowed",
        [&]() {
            bool result = fast_bucket.try_acquire();
            DoNotOptimize(result);
        },
        SLOSpec{.name = "bucket_allowed", .p50_ns = 50, .p99_ns = 500}
    );

    // Token bucket - rate limited
    TokenBucket slow_bucket(RateLimitConfig{
        .rate_per_second = 100,
        .burst_size = 1
    });

    // Drain the bucket first
    while (slow_bucket.try_acquire()) {}

    suite.add_benchmark(
        "token_bucket_try_acquire_limited",
        [&]() {
            bool result = slow_bucket.try_acquire();
            DoNotOptimize(result);
        },
        SLOSpec{.name = "bucket_limited", .p50_ns = 50, .p99_ns = 500}
    );

    // Sliding window
    SlidingWindowLimiter sliding(100000);

    suite.add_benchmark(
        "sliding_window_try_acquire",
        [&]() {
            bool result = sliding.try_acquire();
            DoNotOptimize(result);
        },
        SLOSpec{.name = "sliding", .p50_ns = 100, .p99_ns = 1000}
    );

    BenchmarkConfig config;
    config.iterations = ITERATIONS;
    config.warmup_iterations = WARMUP;

    suite.run(config);
    suite.print_results();

    std::cout << "\nRate Limiter Stats:\n";
    std::cout << "  Fast bucket allow rate: " << fast_bucket.stats().allow_rate() << "%\n";
    std::cout << "  Slow bucket allow rate: " << slow_bucket.stats().allow_rate() << "%\n";
}

/**
 * @brief Backpressure Controller Benchmarks
 */
void run_backpressure_benchmarks() {
    BenchmarkSuite suite("Backpressure Controller");

    // Controller with throttle strategy
    BackpressureController throttle_ctrl(BackpressureConfig{
        .strategy = BackpressureStrategy::THROTTLE,
        .low_watermark = 0.9,   // Start late to avoid throttling
        .high_watermark = 0.95,
        .critical_watermark = 0.99
    });

    suite.add_benchmark(
        "backpressure_should_accept_no_pressure",
        [&]() {
            bool result = throttle_ctrl.should_accept();
            DoNotOptimize(result);
            if (result) throttle_ctrl.item_processed();
        },
        SLOSpec{.name = "bp_no_pressure", .p50_ns = 50, .p99_ns = 500}
    );

    // Controller under pressure
    BackpressureController pressure_ctrl(BackpressureConfig{
        .strategy = BackpressureStrategy::DROP_NEWEST,
        .low_watermark = 0.1,
        .high_watermark = 0.2,
        .critical_watermark = 0.3
    });
    pressure_ctrl.update_queue(90, 100);  // 90% full

    suite.add_benchmark(
        "backpressure_should_accept_under_pressure",
        [&]() {
            bool result = pressure_ctrl.should_accept();
            DoNotOptimize(result);
        },
        SLOSpec{.name = "bp_pressure", .p50_ns = 100, .p99_ns = 1000}
    );

    // Pressure sensor
    PressureSensor sensor;

    suite.add_benchmark(
        "pressure_sensor_update_and_check",
        [&]() {
            sensor.update_queue_fill(50, 100);
            sensor.update_latency(1000000);  // 1ms
            auto level = sensor.level();
            DoNotOptimize(level);
        },
        SLOSpec{.name = "sensor", .p50_ns = 50, .p99_ns = 500}
    );

    BenchmarkConfig config;
    config.iterations = ITERATIONS;
    config.warmup_iterations = WARMUP;

    suite.run(config);
    suite.print_results();
}

/**
 * @brief Cache-Optimized Data Structure Benchmarks
 */
void run_cache_benchmarks() {
    BenchmarkSuite suite("Cache Optimized Structures");

    // Prefetch buffer
    PrefetchBuffer<uint64_t, 1024> prefetch_buf;
    uint64_t counter = 0;

    suite.add_benchmark(
        "prefetch_buffer_push",
        [&]() {
            prefetch_buf.push(counter++);
        },
        [&]() { /* setup */ },
        [&]() {
            uint64_t v;
            while (prefetch_buf.pop(v)) {}
            counter = 0;
        },
        SLOSpec{.name = "prefetch_push", .p50_ns = 50, .p99_ns = 500}
    );

    suite.add_benchmark(
        "prefetch_buffer_pop",
        [&]() {
            uint64_t v;
            bool result = prefetch_buf.pop(v);
            DoNotOptimize(result);
        },
        [&]() {
            for (size_t i = 0; i < 500; ++i) {
                prefetch_buf.push(i);
            }
        },
        [&]() { /* teardown */ }
    );

    // Cache-aligned value
    CacheAligned<uint64_t> aligned_val{0};
    uint64_t regular_val = 0;

    suite.add_benchmark(
        "cache_aligned_increment",
        [&]() {
            aligned_val.value++;
            DoNotOptimize(aligned_val.value);
        }
    );

    suite.add_benchmark(
        "regular_increment",
        [&]() {
            regular_val++;
            DoNotOptimize(regular_val);
        }
    );

    // Batch processor
    std::vector<uint64_t> data(10000);
    std::iota(data.begin(), data.end(), 0);

    suite.add_benchmark(
        "batch_processor_transform",
        [&]() {
            BatchProcessor<uint64_t>::process(
                data.data(), data.size(),
                [](uint64_t& v) { v *= 2; }
            );
            DoNotOptimize(data[0]);
        },
        [&]() {
            std::iota(data.begin(), data.end(), 0);
        },
        [&]() { /* teardown */ }
    );

    // Per-CPU data
    PerCPUData<uint64_t> per_cpu_counter(0);

    suite.add_benchmark(
        "per_cpu_local_increment",
        [&]() {
            per_cpu_counter.local()++;
            DoNotOptimize(per_cpu_counter.local());
        }
    );

    BenchmarkConfig config;
    config.iterations = ITERATIONS;
    config.warmup_iterations = WARMUP;

    suite.run(config);
    suite.print_results();
}

/**
 * @brief Multi-threaded contention benchmarks
 */
void run_contention_benchmarks() {
    BenchmarkSuite suite("Multi-threaded Contention");

    // MPMC queue under contention
    BoundedMPMCQueue<uint64_t> contended_queue(4096);
    std::atomic<bool> running{true};
    std::atomic<uint64_t> ops_completed{0};

    // Start consumer threads
    std::vector<std::thread> consumers;
    for (int i = 0; i < 2; ++i) {
        consumers.emplace_back([&]() {
            while (running.load(std::memory_order_relaxed)) {
                auto result = contended_queue.try_dequeue();
                if (result) {
                    ops_completed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    suite.add_benchmark(
        "mpmc_contended_enqueue",
        [&]() {
            contended_queue.try_enqueue(42);
        }
    );

    BenchmarkConfig config;
    config.iterations = 50000;  // Fewer iterations for contended test
    config.warmup_iterations = 100;

    suite.run(config);

    running.store(false);
    for (auto& t : consumers) {
        t.join();
    }

    suite.print_results();
    std::cout << "Consumer ops completed: " << ops_completed.load() << "\n";
}

} // anonymous namespace

int main(int argc, char** argv) {
    std::cout << "========================================\n";
    std::cout << "  IPB Performance Benchmark Suite\n";
    std::cout << "========================================\n\n";

    // Check for specific benchmark selection
    std::string filter;
    if (argc > 1) {
        filter = argv[1];
    }

    if (filter.empty() || filter == "pool") {
        run_memory_pool_benchmarks();
        std::cout << "\n";
    }

    if (filter.empty() || filter == "queue") {
        run_queue_benchmarks();
        std::cout << "\n";
    }

    if (filter.empty() || filter == "rate") {
        run_rate_limiter_benchmarks();
        std::cout << "\n";
    }

    if (filter.empty() || filter == "backpressure") {
        run_backpressure_benchmarks();
        std::cout << "\n";
    }

    if (filter.empty() || filter == "cache") {
        run_cache_benchmarks();
        std::cout << "\n";
    }

    if (filter.empty() || filter == "contention") {
        run_contention_benchmarks();
        std::cout << "\n";
    }

    std::cout << "========================================\n";
    std::cout << "  Benchmarks Complete\n";
    std::cout << "========================================\n";

    return 0;
}
