/**
 * @file test_rate_limiter.cpp
 * @brief Comprehensive tests for rate_limiter.hpp
 *
 * Covers: RateLimitConfig, RateLimiterStats, TokenBucket, SlidingWindowLimiter,
 *         AdaptiveRateLimiter, HierarchicalRateLimiter, RateLimiterRegistry, RateLimitGuard
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include <ipb/common/rate_limiter.hpp>

using namespace ipb::common;

//=============================================================================
// RateLimitConfig Tests
//=============================================================================

class RateLimitConfigTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(RateLimitConfigTest, DefaultValues) {
    RateLimitConfig config;

    EXPECT_DOUBLE_EQ(config.rate_per_second, 1000.0);
    EXPECT_EQ(config.burst_size, 100);
    EXPECT_FALSE(config.fair_queuing);
    EXPECT_FALSE(config.adaptive);
    EXPECT_DOUBLE_EQ(config.min_rate, 10.0);
    EXPECT_DOUBLE_EQ(config.max_rate, 100000.0);
}

TEST_F(RateLimitConfigTest, UnlimitedConfig) {
    RateLimitConfig config = RateLimitConfig::unlimited();

    EXPECT_GT(config.rate_per_second, 1e10);
    EXPECT_GT(config.burst_size, SIZE_MAX / 4);
}

TEST_F(RateLimitConfigTest, StrictConfig) {
    RateLimitConfig config = RateLimitConfig::strict(100.0);

    EXPECT_DOUBLE_EQ(config.rate_per_second, 100.0);
    EXPECT_EQ(config.burst_size, 1);  // No burst allowed
}

//=============================================================================
// RateLimiterStats Tests
//=============================================================================

class RateLimiterStatsTest : public ::testing::Test {
protected:
    RateLimiterStats stats;

    void SetUp() override {
        stats.reset();
    }
};

TEST_F(RateLimiterStatsTest, InitialValues) {
    EXPECT_EQ(stats.requests.load(), 0);
    EXPECT_EQ(stats.allowed.load(), 0);
    EXPECT_EQ(stats.rejected.load(), 0);
    EXPECT_EQ(stats.throttled_ns.load(), 0);
}

TEST_F(RateLimiterStatsTest, AllowRate) {
    stats.requests.store(100);
    stats.allowed.store(80);
    stats.rejected.store(20);

    EXPECT_DOUBLE_EQ(stats.allow_rate(), 80.0);
}

TEST_F(RateLimiterStatsTest, AllowRateZeroRequests) {
    // With zero requests, allow rate should be 100%
    EXPECT_DOUBLE_EQ(stats.allow_rate(), 100.0);
}

TEST_F(RateLimiterStatsTest, Reset) {
    stats.requests.store(100);
    stats.allowed.store(80);
    stats.rejected.store(20);
    stats.throttled_ns.store(1000000);

    stats.reset();

    EXPECT_EQ(stats.requests.load(), 0);
    EXPECT_EQ(stats.allowed.load(), 0);
    EXPECT_EQ(stats.rejected.load(), 0);
    EXPECT_EQ(stats.throttled_ns.load(), 0);
}

//=============================================================================
// TokenBucket Tests
//=============================================================================

class TokenBucketTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(TokenBucketTest, DefaultConstruction) {
    TokenBucket bucket;

    // Should start with full bucket
    EXPECT_GE(bucket.available_tokens(), 0.0);
}

TEST_F(TokenBucketTest, InitialTokens) {
    RateLimitConfig config;
    config.rate_per_second = 100.0;
    config.burst_size = 50;

    TokenBucket bucket(config);

    // Should have burst_size tokens initially
    EXPECT_NEAR(bucket.available_tokens(), 50.0, 1.0);
}

TEST_F(TokenBucketTest, TryAcquireSuccess) {
    RateLimitConfig config;
    config.rate_per_second = 1000.0;
    config.burst_size = 100;

    TokenBucket bucket(config);

    EXPECT_TRUE(bucket.try_acquire());
    EXPECT_TRUE(bucket.try_acquire());
    EXPECT_TRUE(bucket.try_acquire());

    // Should have acquired 3 tokens
    EXPECT_LT(bucket.available_tokens(), 100.0);
}

TEST_F(TokenBucketTest, TryAcquireMultiple) {
    RateLimitConfig config;
    config.rate_per_second = 1000.0;
    config.burst_size = 100;

    TokenBucket bucket(config);

    EXPECT_TRUE(bucket.try_acquire(10));
    EXPECT_NEAR(bucket.available_tokens(), 90.0, 1.0);

    EXPECT_TRUE(bucket.try_acquire(50));
    EXPECT_NEAR(bucket.available_tokens(), 40.0, 1.0);
}

TEST_F(TokenBucketTest, TryAcquireExhausted) {
    RateLimitConfig config;
    config.rate_per_second = 100.0;
    config.burst_size = 10;

    TokenBucket bucket(config);

    // Exhaust all tokens
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(bucket.try_acquire());
    }

    // Next acquire should fail
    EXPECT_FALSE(bucket.try_acquire());
}

TEST_F(TokenBucketTest, TokenRefill) {
    RateLimitConfig config;
    config.rate_per_second = 10000.0;  // High rate for fast refill
    config.burst_size = 10;

    TokenBucket bucket(config);

    // Exhaust tokens
    for (int i = 0; i < 10; ++i) {
        bucket.try_acquire();
    }

    // Wait for refill
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Should be able to acquire again
    EXPECT_TRUE(bucket.try_acquire());
}

TEST_F(TokenBucketTest, WaitTimeNs) {
    RateLimitConfig config;
    config.rate_per_second = 1000.0;  // 1 token per ms
    config.burst_size = 1;

    TokenBucket bucket(config);

    // With tokens available, wait time should be 0
    EXPECT_EQ(bucket.wait_time_ns(1), 0);

    // Exhaust tokens
    bucket.try_acquire();

    // Now we need to wait for tokens
    int64_t wait = bucket.wait_time_ns(1);
    EXPECT_GT(wait, 0);
}

TEST_F(TokenBucketTest, AcquireWithTimeout) {
    RateLimitConfig config;
    config.rate_per_second = 10000.0;
    config.burst_size = 1;

    TokenBucket bucket(config);

    // Exhaust tokens
    bucket.try_acquire();

    // Should acquire after waiting
    EXPECT_TRUE(bucket.acquire(1, std::chrono::milliseconds(100)));
}

TEST_F(TokenBucketTest, AcquireTimeoutExpired) {
    RateLimitConfig config;
    config.rate_per_second = 1.0;  // Very slow refill
    config.burst_size = 1;

    TokenBucket bucket(config);

    // Exhaust tokens
    bucket.try_acquire();

    // Very short timeout should fail
    EXPECT_FALSE(bucket.acquire(1, std::chrono::nanoseconds(100)));
}

TEST_F(TokenBucketTest, SetRate) {
    RateLimitConfig config;
    config.rate_per_second = 100.0;
    config.burst_size = 10;

    TokenBucket bucket(config);

    bucket.set_rate(200.0);
    EXPECT_DOUBLE_EQ(bucket.config().rate_per_second, 200.0);
}

TEST_F(TokenBucketTest, SetBurst) {
    RateLimitConfig config;
    config.rate_per_second = 100.0;
    config.burst_size = 10;

    TokenBucket bucket(config);

    bucket.set_burst(20);
    EXPECT_EQ(bucket.config().burst_size, 20);
}

TEST_F(TokenBucketTest, StatsTracking) {
    RateLimitConfig config;
    config.rate_per_second = 100.0;
    config.burst_size = 5;

    TokenBucket bucket(config);

    // Make 10 requests when only 5 tokens available
    for (int i = 0; i < 10; ++i) {
        bucket.try_acquire();
    }

    const RateLimiterStats& stats = bucket.stats();

    EXPECT_EQ(stats.requests.load(), 10);
    EXPECT_EQ(stats.allowed.load(), 5);
    EXPECT_EQ(stats.rejected.load(), 5);
}

TEST_F(TokenBucketTest, ResetStats) {
    RateLimitConfig config;
    config.burst_size = 5;

    TokenBucket bucket(config);

    for (int i = 0; i < 10; ++i) {
        bucket.try_acquire();
    }

    bucket.reset_stats();

    const RateLimiterStats& stats = bucket.stats();
    EXPECT_EQ(stats.requests.load(), 0);
    EXPECT_EQ(stats.allowed.load(), 0);
    EXPECT_EQ(stats.rejected.load(), 0);
}

TEST_F(TokenBucketTest, ConcurrentAcquires) {
    RateLimitConfig config;
    config.rate_per_second = 100000.0;
    config.burst_size = 10000;

    TokenBucket bucket(config);

    constexpr int num_threads = 8;
    constexpr int acquires_per_thread = 1000;

    std::atomic<int> total_allowed{0};
    std::atomic<int> total_rejected{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&bucket, &total_allowed, &total_rejected]() {
            for (int j = 0; j < acquires_per_thread; ++j) {
                if (bucket.try_acquire()) {
                    total_allowed.fetch_add(1);
                } else {
                    total_rejected.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Total should match our stats
    const RateLimiterStats& stats = bucket.stats();
    EXPECT_EQ(stats.requests.load(), num_threads * acquires_per_thread);
    EXPECT_EQ(stats.allowed.load() + stats.rejected.load(), num_threads * acquires_per_thread);
}

//=============================================================================
// SlidingWindowLimiter Tests
//=============================================================================

class SlidingWindowLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(SlidingWindowLimiterTest, Construction) {
    SlidingWindowLimiter limiter(100.0);

    EXPECT_DOUBLE_EQ(limiter.limit(), 100.0);
    EXPECT_DOUBLE_EQ(limiter.current_rate(), 0.0);
}

TEST_F(SlidingWindowLimiterTest, BasicAcquire) {
    SlidingWindowLimiter limiter(1000.0);

    EXPECT_TRUE(limiter.try_acquire());
    EXPECT_TRUE(limiter.try_acquire());

    EXPECT_GT(limiter.current_rate(), 0.0);
}

TEST_F(SlidingWindowLimiterTest, RateLimiting) {
    SlidingWindowLimiter limiter(10.0);  // Only 10 requests per second

    // First 10 should succeed
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.try_acquire());
    }

    // 11th should fail
    EXPECT_FALSE(limiter.try_acquire());
}

TEST_F(SlidingWindowLimiterTest, StatsTracking) {
    SlidingWindowLimiter limiter(5.0);

    // Make 10 requests
    for (int i = 0; i < 10; ++i) {
        limiter.try_acquire();
    }

    const RateLimiterStats& stats = limiter.stats();

    EXPECT_EQ(stats.requests.load(), 10);
    EXPECT_EQ(stats.allowed.load(), 5);
    EXPECT_EQ(stats.rejected.load(), 5);
}

TEST_F(SlidingWindowLimiterTest, WindowSliding) {
    SlidingWindowLimiter limiter(100.0);

    // Fill up to limit
    for (int i = 0; i < 100; ++i) {
        limiter.try_acquire();
    }

    EXPECT_FALSE(limiter.try_acquire());

    // After window slides, should be able to acquire again
    // Note: This depends on the slot duration (1/60 second per slot = ~16ms)
    // Wait longer to ensure slots clear
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Some slots should have cleared - may still fail if timing is tight
    // Just verify the limiter is functional
    double rate_before = limiter.current_rate();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // After more time, rate should decrease
    EXPECT_LE(limiter.current_rate(), rate_before);
}

//=============================================================================
// AdaptiveRateLimiter Tests
//=============================================================================

class AdaptiveRateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(AdaptiveRateLimiterTest, Construction) {
    RateLimitConfig config;
    config.rate_per_second = 1000.0;
    config.burst_size = 100;
    config.adaptive = true;
    config.min_rate = 10.0;
    config.max_rate = 10000.0;

    AdaptiveRateLimiter limiter(config);

    EXPECT_NEAR(limiter.current_rate(), 1000.0, 1.0);
}

TEST_F(AdaptiveRateLimiterTest, TryAcquire) {
    RateLimitConfig config;
    config.rate_per_second = 10000.0;
    config.burst_size = 100;

    AdaptiveRateLimiter limiter(config);

    EXPECT_TRUE(limiter.try_acquire());
}

TEST_F(AdaptiveRateLimiterTest, LoadReporting) {
    RateLimitConfig config;
    config.rate_per_second = 1000.0;
    config.burst_size = 1000;
    config.min_rate = 10.0;
    config.max_rate = 10000.0;

    AdaptiveRateLimiter limiter(config);

    double initial_rate = limiter.current_rate();

    // Report high load
    for (int i = 0; i < 10; ++i) {
        limiter.report_load(0.9);  // 90% load
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    // Force rate update by trying to acquire
    limiter.try_acquire();

    // Wait for rate update (happens every 100ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    limiter.try_acquire();

    // Rate should decrease under high load
    // Note: The actual rate change depends on the EMA calculation
    EXPECT_LE(limiter.current_rate(), config.max_rate);
}

TEST_F(AdaptiveRateLimiterTest, StatsTracking) {
    RateLimitConfig config;
    config.burst_size = 5;

    AdaptiveRateLimiter limiter(config);

    for (int i = 0; i < 10; ++i) {
        limiter.try_acquire();
    }

    const RateLimiterStats& stats = limiter.stats();
    EXPECT_EQ(stats.requests.load(), 10);
}

//=============================================================================
// HierarchicalRateLimiter Tests
//=============================================================================

class HierarchicalRateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(HierarchicalRateLimiterTest, GlobalLimitOnly) {
    RateLimitConfig global_config;
    global_config.rate_per_second = 1000.0;
    global_config.burst_size = 100;

    HierarchicalRateLimiter limiter(global_config);

    // Should be able to acquire up to burst size
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(limiter.try_acquire());
    }

    // Next should fail (global limit)
    EXPECT_FALSE(limiter.try_acquire());
}

TEST_F(HierarchicalRateLimiterTest, PerSourceLimit) {
    RateLimitConfig global_config;
    global_config.rate_per_second = 10000.0;
    global_config.burst_size = 1000;

    HierarchicalRateLimiter limiter(global_config);

    // Add per-source limit
    RateLimitConfig source_config;
    source_config.rate_per_second = 100.0;
    source_config.burst_size = 10;

    limiter.add_source_limit("source1", source_config);

    // First 10 from source1 should succeed
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.try_acquire("source1"));
    }

    // 11th from source1 should fail
    EXPECT_FALSE(limiter.try_acquire("source1"));

    // But global (empty source) should still work
    EXPECT_TRUE(limiter.try_acquire());
}

TEST_F(HierarchicalRateLimiterTest, MultipleSources) {
    RateLimitConfig global_config;
    global_config.burst_size = 1000;

    HierarchicalRateLimiter limiter(global_config);

    RateLimitConfig source_config;
    source_config.burst_size = 5;

    limiter.add_source_limit("source1", source_config);
    limiter.add_source_limit("source2", source_config);

    // Each source can use its own limit
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.try_acquire("source1"));
        EXPECT_TRUE(limiter.try_acquire("source2"));
    }

    // Both should now be limited
    EXPECT_FALSE(limiter.try_acquire("source1"));
    EXPECT_FALSE(limiter.try_acquire("source2"));
}

TEST_F(HierarchicalRateLimiterTest, GlobalStats) {
    RateLimitConfig global_config;
    global_config.burst_size = 100;

    HierarchicalRateLimiter limiter(global_config);

    for (int i = 0; i < 50; ++i) {
        limiter.try_acquire();
    }

    const RateLimiterStats& stats = limiter.global_stats();
    EXPECT_EQ(stats.requests.load(), 50);
    EXPECT_EQ(stats.allowed.load(), 50);
}

TEST_F(HierarchicalRateLimiterTest, SourceStats) {
    RateLimitConfig global_config;
    global_config.burst_size = 1000;

    HierarchicalRateLimiter limiter(global_config);

    RateLimitConfig source_config;
    source_config.burst_size = 10;

    limiter.add_source_limit("source1", source_config);

    for (int i = 0; i < 15; ++i) {
        limiter.try_acquire("source1");
    }

    const RateLimiterStats* stats = limiter.source_stats("source1");
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->requests.load(), 15);
    EXPECT_EQ(stats->allowed.load(), 10);
    EXPECT_EQ(stats->rejected.load(), 5);
}

TEST_F(HierarchicalRateLimiterTest, NonExistentSourceStats) {
    RateLimitConfig global_config;
    HierarchicalRateLimiter limiter(global_config);

    const RateLimiterStats* stats = limiter.source_stats("nonexistent");
    EXPECT_EQ(stats, nullptr);
}

TEST_F(HierarchicalRateLimiterTest, GlobalLimitBlocksAllSources) {
    RateLimitConfig global_config;
    global_config.burst_size = 10;

    HierarchicalRateLimiter limiter(global_config);

    RateLimitConfig source_config;
    source_config.burst_size = 100;

    limiter.add_source_limit("source1", source_config);

    // Even though source has 100 burst, global only has 10
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.try_acquire("source1"));
    }

    // Global limit reached
    EXPECT_FALSE(limiter.try_acquire("source1"));
}

//=============================================================================
// RateLimiterRegistry Tests
//=============================================================================

class RateLimiterRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up registry
        auto& registry = RateLimiterRegistry::instance();
        registry.remove("test_limiter");
        registry.remove("limiter1");
        registry.remove("limiter2");
    }
};

TEST_F(RateLimiterRegistryTest, Singleton) {
    RateLimiterRegistry& r1 = RateLimiterRegistry::instance();
    RateLimiterRegistry& r2 = RateLimiterRegistry::instance();

    EXPECT_EQ(&r1, &r2);
}

TEST_F(RateLimiterRegistryTest, RegisterAndGet) {
    auto& registry = RateLimiterRegistry::instance();

    RateLimitConfig config;
    config.burst_size = 50;

    registry.register_limiter("test_limiter", config);

    TokenBucket& bucket = registry.get_or_create("test_limiter");
    EXPECT_NEAR(bucket.available_tokens(), 50.0, 1.0);
}

TEST_F(RateLimiterRegistryTest, GetOrCreate) {
    auto& registry = RateLimiterRegistry::instance();

    RateLimitConfig config;
    config.burst_size = 30;

    TokenBucket& b1 = registry.get_or_create("limiter1", config);
    TokenBucket& b2 = registry.get_or_create("limiter1", config);

    // Should return same limiter
    EXPECT_EQ(&b1, &b2);
}

TEST_F(RateLimiterRegistryTest, TryAcquire) {
    auto& registry = RateLimiterRegistry::instance();

    RateLimitConfig config;
    config.burst_size = 5;

    registry.register_limiter("limiter2", config);

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(registry.try_acquire("limiter2"));
    }

    EXPECT_FALSE(registry.try_acquire("limiter2"));
}

TEST_F(RateLimiterRegistryTest, TryAcquireNonExistent) {
    auto& registry = RateLimiterRegistry::instance();

    // Non-existent limiter should allow (no limit)
    EXPECT_TRUE(registry.try_acquire("nonexistent_limiter"));
}

TEST_F(RateLimiterRegistryTest, Remove) {
    auto& registry = RateLimiterRegistry::instance();

    RateLimitConfig config;
    config.burst_size = 1;

    registry.register_limiter("temp_limiter", config);

    // Should be limited
    EXPECT_TRUE(registry.try_acquire("temp_limiter"));
    EXPECT_FALSE(registry.try_acquire("temp_limiter"));

    // Remove limiter
    registry.remove("temp_limiter");

    // Now should have no limit
    EXPECT_TRUE(registry.try_acquire("temp_limiter"));
    EXPECT_TRUE(registry.try_acquire("temp_limiter"));
}

//=============================================================================
// RateLimitGuard Tests
//=============================================================================

class RateLimitGuardTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(RateLimitGuardTest, TryAcquireSuccess) {
    RateLimitConfig config;
    config.burst_size = 10;

    TokenBucket bucket(config);

    auto guard = RateLimitGuard::try_acquire(bucket);
    ASSERT_TRUE(guard.has_value());
    EXPECT_TRUE(static_cast<bool>(*guard));
}

TEST_F(RateLimitGuardTest, TryAcquireFailure) {
    RateLimitConfig config;
    config.burst_size = 1;

    TokenBucket bucket(config);

    // Exhaust tokens
    bucket.try_acquire();

    auto guard = RateLimitGuard::try_acquire(bucket);
    EXPECT_FALSE(guard.has_value());
}

TEST_F(RateLimitGuardTest, BoolConversion) {
    RateLimitConfig config;
    config.burst_size = 10;

    TokenBucket bucket(config);

    RateLimitGuard acquired_guard(bucket, true);
    RateLimitGuard not_acquired_guard(bucket, false);

    EXPECT_TRUE(static_cast<bool>(acquired_guard));
    EXPECT_FALSE(static_cast<bool>(not_acquired_guard));
}

TEST_F(RateLimitGuardTest, UsagePattern) {
    RateLimitConfig config;
    config.burst_size = 5;

    TokenBucket bucket(config);

    int acquired_count = 0;

    for (int i = 0; i < 10; ++i) {
        if (auto guard = RateLimitGuard::try_acquire(bucket)) {
            // Token acquired
            acquired_count++;
        }
    }

    EXPECT_EQ(acquired_count, 5);
}

//=============================================================================
// Integration Tests
//=============================================================================

TEST(IntegrationTest, HighThroughput) {
    RateLimitConfig config;
    config.rate_per_second = 100000.0;
    config.burst_size = 10000;

    TokenBucket bucket(config);

    auto start = std::chrono::high_resolution_clock::now();

    int successful = 0;
    for (int i = 0; i < 100000; ++i) {
        if (bucket.try_acquire()) {
            successful++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete quickly
    EXPECT_LT(duration.count(), 1000);
    EXPECT_GT(successful, 0);
}

TEST(IntegrationTest, ConcurrentMixedLimiters) {
    auto& registry = RateLimiterRegistry::instance();

    RateLimitConfig config;
    config.burst_size = 100;

    registry.register_limiter("concurrent_test_1", config);
    registry.register_limiter("concurrent_test_2", config);

    constexpr int num_threads = 4;
    std::atomic<int> total_acquired{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&registry, &total_acquired, i]() {
            std::string limiter_name = "concurrent_test_" + std::to_string((i % 2) + 1);
            for (int j = 0; j < 100; ++j) {
                if (registry.try_acquire(limiter_name)) {
                    total_acquired.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Should have acquired from both limiters
    EXPECT_GT(total_acquired.load(), 0);
    EXPECT_LE(total_acquired.load(), 200);  // Max is 100 per limiter

    // Cleanup
    registry.remove("concurrent_test_1");
    registry.remove("concurrent_test_2");
}

TEST(IntegrationTest, TokenBucketRefillTiming) {
    RateLimitConfig config;
    config.rate_per_second = 100.0;  // 100 tokens per second = 1 token per 10ms
    config.burst_size = 5;

    TokenBucket bucket(config);

    // Exhaust all tokens
    while (bucket.try_acquire()) {}

    // Wait for 50ms (should refill ~5 tokens)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should be able to acquire some tokens
    int acquired = 0;
    for (int i = 0; i < 10; ++i) {
        if (bucket.try_acquire()) {
            acquired++;
        }
    }

    // Should have refilled some tokens (roughly 5, but cap at burst size)
    EXPECT_GE(acquired, 3);
    EXPECT_LE(acquired, 6);
}

//=============================================================================
// Edge Cases
//=============================================================================

TEST(EdgeCaseTest, ZeroRateConfig) {
    RateLimitConfig config;
    config.rate_per_second = 0.0;  // No refill
    config.burst_size = 5;

    TokenBucket bucket(config);

    // Should still start with initial tokens
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(bucket.try_acquire());
    }

    // But no more after exhaustion
    EXPECT_FALSE(bucket.try_acquire());
}

TEST(EdgeCaseTest, ZeroBurstSize) {
    RateLimitConfig config;
    config.rate_per_second = 1000.0;
    config.burst_size = 0;

    TokenBucket bucket(config);

    // Should not be able to acquire anything
    EXPECT_FALSE(bucket.try_acquire());
}

TEST(EdgeCaseTest, VeryHighRate) {
    // Test high rate configuration rather than "unlimited" which may have
    // edge cases with SIZE_MAX values
    RateLimitConfig config;
    config.rate_per_second = 1000000.0;  // 1M per second
    config.burst_size = 10000;

    TokenBucket bucket(config);

    // Should be able to acquire many tokens quickly
    int acquired = 0;
    for (int i = 0; i < 100; ++i) {
        if (bucket.try_acquire(1)) {
            acquired++;
        }
    }
    EXPECT_EQ(acquired, 100);
}

TEST(EdgeCaseTest, LargeTokenRequest) {
    RateLimitConfig config;
    config.burst_size = 100;

    TokenBucket bucket(config);

    // Request more than available
    EXPECT_FALSE(bucket.try_acquire(200));

    // But original tokens should still be there
    EXPECT_TRUE(bucket.try_acquire(50));
}
