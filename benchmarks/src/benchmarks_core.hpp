#pragma once

/**
 * @file benchmarks_core.hpp
 * @brief Core component benchmarks
 *
 * Benchmarks for:
 * - Router (routing, filtering, transformation)
 * - Memory Pool (allocation, deallocation)
 * - Lock-free Queues (SPSC, MPSC, MPMC)
 * - Rate Limiter
 * - Backpressure Controller
 * - Pattern Matcher
 * - Data Point operations
 */

#include <ipb/benchmarks/benchmark_framework.hpp>
#include <ipb/common/backpressure.hpp>
#include <ipb/common/cache_optimized.hpp>
#include <ipb/common/data_point.hpp>
#include <ipb/common/lockfree_queue.hpp>
#include <ipb/common/memory_pool.hpp>
#include <ipb/common/rate_limiter.hpp>

#include <atomic>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

namespace ipb::benchmark {

// Test structures
struct BenchmarkData {
    uint64_t id;
    double value;
    char payload[48];  // 64 bytes total
};

// Prevent compiler optimization
template <typename T>
inline void do_not_optimize(T&& value) {
#ifdef __GNUC__
    asm volatile("" : : "r,m"(value) : "memory");
#else
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

//=============================================================================
// Memory Pool Benchmarks
//=============================================================================

namespace memory_pool_benchmarks {

inline common::ObjectPool<BenchmarkData, 256>* g_pool = nullptr;
inline BenchmarkData* g_allocated                     = nullptr;

void setup() {
    if (!g_pool) {
        g_pool = new common::ObjectPool<BenchmarkData, 256>(1024);
    }
}

void bench_allocate() {
    g_allocated = g_pool->allocate();
    do_not_optimize(g_allocated);
}

void teardown_allocate() {
    if (g_allocated) {
        g_pool->deallocate(g_allocated);
        g_allocated = nullptr;
    }
}

void setup_deallocate() {
    setup();
    g_allocated = g_pool->allocate();
}

void bench_deallocate() {
    g_pool->deallocate(g_allocated);
    g_allocated = nullptr;
}

void bench_alloc_dealloc_cycle() {
    auto* ptr = g_pool->allocate();
    do_not_optimize(ptr);
    g_pool->deallocate(ptr);
}

void bench_heap_new_delete() {
    auto* ptr = new BenchmarkData();
    do_not_optimize(ptr);
    delete ptr;
}

void cleanup() {
    delete g_pool;
    g_pool = nullptr;
}

}  // namespace memory_pool_benchmarks

//=============================================================================
// Lock-free Queue Benchmarks
//=============================================================================

namespace queue_benchmarks {

inline common::SPSCQueue<uint64_t, 4096>* g_spsc  = nullptr;
inline common::BoundedMPMCQueue<uint64_t>* g_mpmc = nullptr;
inline uint64_t g_counter                         = 0;

void setup_spsc() {
    if (!g_spsc) {
        g_spsc = new common::SPSCQueue<uint64_t, 4096>();
    }
    g_counter = 0;
}

void bench_spsc_enqueue() {
    g_spsc->try_enqueue(g_counter++);
}

void teardown_spsc_enqueue() {
    while (g_spsc->try_dequeue()) {}
}

void setup_spsc_dequeue() {
    setup_spsc();
    for (size_t i = 0; i < 1000; ++i) {
        g_spsc->try_enqueue(i);
    }
}

void bench_spsc_dequeue() {
    auto result = g_spsc->try_dequeue();
    do_not_optimize(result);
}

void bench_spsc_cycle() {
    g_spsc->try_enqueue(g_counter++);
    auto result = g_spsc->try_dequeue();
    do_not_optimize(result);
}

void setup_mpmc() {
    if (!g_mpmc) {
        g_mpmc = new common::BoundedMPMCQueue<uint64_t>(4096);
    }
    g_counter = 0;
}

void bench_mpmc_enqueue() {
    g_mpmc->try_enqueue(g_counter++);
}

void teardown_mpmc_enqueue() {
    while (g_mpmc->try_dequeue()) {}
}

void setup_mpmc_dequeue() {
    setup_mpmc();
    for (size_t i = 0; i < 1000; ++i) {
        g_mpmc->try_enqueue(i);
    }
}

void bench_mpmc_dequeue() {
    auto result = g_mpmc->try_dequeue();
    do_not_optimize(result);
}

void bench_mpmc_cycle() {
    g_mpmc->try_enqueue(g_counter++);
    auto result = g_mpmc->try_dequeue();
    do_not_optimize(result);
}

void cleanup() {
    delete g_spsc;
    delete g_mpmc;
    g_spsc = nullptr;
    g_mpmc = nullptr;
}

}  // namespace queue_benchmarks

//=============================================================================
// Rate Limiter Benchmarks
//=============================================================================

namespace rate_limiter_benchmarks {

inline common::TokenBucket* g_fast_bucket      = nullptr;
inline common::TokenBucket* g_slow_bucket      = nullptr;
inline common::SlidingWindowLimiter* g_sliding = nullptr;

void setup() {
    if (!g_fast_bucket) {
        g_fast_bucket = new common::TokenBucket(
            common::RateLimitConfig{.rate_per_second = 10000000, .burst_size = 100000});
    }
    if (!g_slow_bucket) {
        g_slow_bucket = new common::TokenBucket(
            common::RateLimitConfig{.rate_per_second = 100, .burst_size = 1});
        // Drain the bucket
        while (g_slow_bucket->try_acquire()) {}
    }
    if (!g_sliding) {
        g_sliding = new common::SlidingWindowLimiter(10000000);
    }
}

void bench_token_bucket_allowed() {
    bool result = g_fast_bucket->try_acquire();
    do_not_optimize(result);
}

void bench_token_bucket_limited() {
    bool result = g_slow_bucket->try_acquire();
    do_not_optimize(result);
}

void bench_sliding_window() {
    bool result = g_sliding->try_acquire();
    do_not_optimize(result);
}

void cleanup() {
    delete g_fast_bucket;
    delete g_slow_bucket;
    delete g_sliding;
    g_fast_bucket = nullptr;
    g_slow_bucket = nullptr;
    g_sliding     = nullptr;
}

}  // namespace rate_limiter_benchmarks

//=============================================================================
// Backpressure Benchmarks
//=============================================================================

namespace backpressure_benchmarks {

inline common::BackpressureController* g_no_pressure   = nullptr;
inline common::BackpressureController* g_high_pressure = nullptr;
inline common::PressureSensor* g_sensor                = nullptr;

void setup() {
    if (!g_no_pressure) {
        g_no_pressure = new common::BackpressureController(
            common::BackpressureConfig{.strategy           = common::BackpressureStrategy::THROTTLE,
                                       .low_watermark      = 0.9,
                                       .high_watermark     = 0.95,
                                       .critical_watermark = 0.99});
    }
    if (!g_high_pressure) {
        g_high_pressure = new common::BackpressureController(
            common::BackpressureConfig{.strategy       = common::BackpressureStrategy::DROP_NEWEST,
                                       .low_watermark  = 0.1,
                                       .high_watermark = 0.2,
                                       .critical_watermark = 0.3});
        g_high_pressure->update_queue(90, 100);
    }
    if (!g_sensor) {
        g_sensor = new common::PressureSensor();
    }
}

void bench_no_pressure() {
    bool result = g_no_pressure->should_accept();
    do_not_optimize(result);
    if (result)
        g_no_pressure->item_processed();
}

void bench_high_pressure() {
    bool result = g_high_pressure->should_accept();
    do_not_optimize(result);
}

void bench_sensor_update() {
    g_sensor->update_queue_fill(50, 100);
    g_sensor->update_latency(1000000);
    auto level = g_sensor->level();
    do_not_optimize(level);
}

void cleanup() {
    delete g_no_pressure;
    delete g_high_pressure;
    delete g_sensor;
    g_no_pressure   = nullptr;
    g_high_pressure = nullptr;
    g_sensor        = nullptr;
}

}  // namespace backpressure_benchmarks

//=============================================================================
// Cache Optimization Benchmarks
//=============================================================================

namespace cache_benchmarks {

inline common::PrefetchBuffer<uint64_t, 1024>* g_prefetch_buf = nullptr;
inline common::CacheAligned<uint64_t>* g_aligned              = nullptr;
inline uint64_t g_regular                                     = 0;
inline uint64_t g_counter                                     = 0;

void setup() {
    if (!g_prefetch_buf) {
        g_prefetch_buf = new common::PrefetchBuffer<uint64_t, 1024>();
    }
    if (!g_aligned) {
        g_aligned = new common::CacheAligned<uint64_t>(0);
    }
    g_counter = 0;
}

void bench_prefetch_push() {
    g_prefetch_buf->push(g_counter++);
}

void teardown_prefetch_push() {
    uint64_t v;
    while (g_prefetch_buf->pop(v)) {}
    g_counter = 0;
}

void setup_prefetch_pop() {
    setup();
    for (size_t i = 0; i < 500; ++i) {
        g_prefetch_buf->push(i);
    }
}

void bench_prefetch_pop() {
    uint64_t v;
    bool result = g_prefetch_buf->pop(v);
    do_not_optimize(result);
}

void bench_aligned_increment() {
    g_aligned->value++;
    do_not_optimize(g_aligned->value);
}

void bench_regular_increment() {
    g_regular++;
    do_not_optimize(g_regular);
}

void cleanup() {
    delete g_prefetch_buf;
    delete g_aligned;
    g_prefetch_buf = nullptr;
    g_aligned      = nullptr;
}

}  // namespace cache_benchmarks

//=============================================================================
// DataPoint Benchmarks
//=============================================================================

namespace datapoint_benchmarks {

inline common::DataPoint* g_dp = nullptr;

void setup() {
    if (!g_dp) {
        g_dp = new common::DataPoint();
        g_dp->set_address("test.sensor.temperature");
        common::Value val;
        val.set(42.5);
        g_dp->set_value(val);
    }
}

void bench_create_datapoint() {
    common::DataPoint dp;
    dp.set_address("sensor.value");
    common::Value val;
    val.set(42.0);
    dp.set_value(val);
    do_not_optimize(dp);
}

void bench_copy_datapoint() {
    common::DataPoint copy = *g_dp;
    do_not_optimize(copy);
}

void bench_value_get() {
    double val = g_dp->value().get<double>();
    do_not_optimize(val);
}

void bench_value_create() {
    common::Value v;
    v.set(3.14159);
    do_not_optimize(v);
}

void cleanup() {
    delete g_dp;
    g_dp = nullptr;
}

}  // namespace datapoint_benchmarks

//=============================================================================
// Registration Function
//=============================================================================

inline void register_core_benchmarks() {
    auto& registry = BenchmarkRegistry::instance();

    // Memory Pool
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::CORE;
        def.component  = "memory_pool";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "allocate";
        def.setup         = memory_pool_benchmarks::setup;
        def.benchmark     = memory_pool_benchmarks::bench_allocate;
        def.teardown      = memory_pool_benchmarks::teardown_allocate;
        def.target_p50_ns = 100;
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name      = "deallocate";
        def.setup     = memory_pool_benchmarks::setup_deallocate;
        def.benchmark = memory_pool_benchmarks::bench_deallocate;
        def.teardown  = nullptr;
        registry.register_benchmark(def);

        def.name          = "alloc_dealloc_cycle";
        def.setup         = memory_pool_benchmarks::setup;
        def.benchmark     = memory_pool_benchmarks::bench_alloc_dealloc_cycle;
        def.teardown      = nullptr;
        def.target_p50_ns = 200;
        def.target_p99_ns = 2000;
        registry.register_benchmark(def);

        def.name          = "heap_new_delete";
        def.setup         = nullptr;
        def.benchmark     = memory_pool_benchmarks::bench_heap_new_delete;
        def.teardown      = nullptr;
        def.target_p50_ns = 500;
        def.target_p99_ns = 5000;
        registry.register_benchmark(def);
    }

    // Lock-free Queues
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::CORE;
        def.component  = "queue";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "spsc_enqueue";
        def.setup         = queue_benchmarks::setup_spsc;
        def.benchmark     = queue_benchmarks::bench_spsc_enqueue;
        def.teardown      = queue_benchmarks::teardown_spsc_enqueue;
        def.target_p50_ns = 50;
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name      = "spsc_dequeue";
        def.setup     = queue_benchmarks::setup_spsc_dequeue;
        def.benchmark = queue_benchmarks::bench_spsc_dequeue;
        def.teardown  = nullptr;
        registry.register_benchmark(def);

        def.name          = "spsc_cycle";
        def.setup         = queue_benchmarks::setup_spsc;
        def.benchmark     = queue_benchmarks::bench_spsc_cycle;
        def.teardown      = nullptr;
        def.target_p50_ns = 100;
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name          = "mpmc_enqueue";
        def.setup         = queue_benchmarks::setup_mpmc;
        def.benchmark     = queue_benchmarks::bench_mpmc_enqueue;
        def.teardown      = queue_benchmarks::teardown_mpmc_enqueue;
        def.target_p50_ns = 100;
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name      = "mpmc_dequeue";
        def.setup     = queue_benchmarks::setup_mpmc_dequeue;
        def.benchmark = queue_benchmarks::bench_mpmc_dequeue;
        def.teardown  = nullptr;
        registry.register_benchmark(def);

        def.name      = "mpmc_cycle";
        def.setup     = queue_benchmarks::setup_mpmc;
        def.benchmark = queue_benchmarks::bench_mpmc_cycle;
        def.teardown  = nullptr;
        registry.register_benchmark(def);
    }

    // Rate Limiter
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::CORE;
        def.component  = "rate_limiter";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "token_bucket_allowed";
        def.setup         = rate_limiter_benchmarks::setup;
        def.benchmark     = rate_limiter_benchmarks::bench_token_bucket_allowed;
        def.target_p50_ns = 100;  // Relaxed for CI environments
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name          = "token_bucket_limited";
        def.setup         = rate_limiter_benchmarks::setup;
        def.benchmark     = rate_limiter_benchmarks::bench_token_bucket_limited;
        def.target_p50_ns = 100;  // Explicit threshold (not inherited)
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);

        def.name          = "sliding_window";
        def.setup         = rate_limiter_benchmarks::setup;
        def.benchmark     = rate_limiter_benchmarks::bench_sliding_window;
        def.target_p50_ns = 100;
        def.target_p99_ns = 1000;
        registry.register_benchmark(def);
    }

    // Backpressure
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::CORE;
        def.component  = "backpressure";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "no_pressure";
        def.setup         = backpressure_benchmarks::setup;
        def.benchmark     = backpressure_benchmarks::bench_no_pressure;
        def.target_p50_ns = 50;
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name      = "high_pressure";
        def.setup     = backpressure_benchmarks::setup;
        def.benchmark = backpressure_benchmarks::bench_high_pressure;
        registry.register_benchmark(def);

        def.name      = "sensor_update";
        def.setup     = backpressure_benchmarks::setup;
        def.benchmark = backpressure_benchmarks::bench_sensor_update;
        registry.register_benchmark(def);
    }

    // Cache Optimization
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::CORE;
        def.component  = "cache";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name      = "prefetch_push";
        def.setup     = cache_benchmarks::setup;
        def.benchmark = cache_benchmarks::bench_prefetch_push;
        def.teardown  = cache_benchmarks::teardown_prefetch_push;
        registry.register_benchmark(def);

        def.name      = "prefetch_pop";
        def.setup     = cache_benchmarks::setup_prefetch_pop;
        def.benchmark = cache_benchmarks::bench_prefetch_pop;
        registry.register_benchmark(def);

        def.name      = "aligned_increment";
        def.setup     = cache_benchmarks::setup;
        def.benchmark = cache_benchmarks::bench_aligned_increment;
        registry.register_benchmark(def);

        def.name      = "regular_increment";
        def.setup     = nullptr;
        def.benchmark = cache_benchmarks::bench_regular_increment;
        registry.register_benchmark(def);
    }

    // DataPoint
    {
        BenchmarkDef def;
        def.category   = BenchmarkCategory::CORE;
        def.component  = "datapoint";
        def.iterations = 100000;
        def.warmup     = 1000;

        def.name          = "create";
        def.setup         = nullptr;
        def.benchmark     = datapoint_benchmarks::bench_create_datapoint;
        def.target_p50_ns = 500;
        def.target_p99_ns = 5000;
        registry.register_benchmark(def);

        def.name      = "copy";
        def.setup     = datapoint_benchmarks::setup;
        def.benchmark = datapoint_benchmarks::bench_copy_datapoint;
        registry.register_benchmark(def);

        def.name          = "value_get";
        def.setup         = datapoint_benchmarks::setup;
        def.benchmark     = datapoint_benchmarks::bench_value_get;
        def.target_p50_ns = 50;   // Relaxed for CI environments
        def.target_p99_ns = 500;
        registry.register_benchmark(def);

        def.name          = "value_create";
        def.setup         = nullptr;
        def.benchmark     = datapoint_benchmarks::bench_value_create;
        def.target_p50_ns = 50;   // Explicit threshold (not inherited)
        def.target_p99_ns = 500;
        registry.register_benchmark(def);
    }
}

}  // namespace ipb::benchmark
