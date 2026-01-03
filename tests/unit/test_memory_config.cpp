/**
 * @file test_memory_config.cpp
 * @brief Comprehensive unit tests for IPB memory configuration system
 *
 * Industrial-grade test coverage including:
 * - Memory profile presets (EMBEDDED, IOT, EDGE, STANDARD, HIGH_PERF)
 * - Configuration validation with boundary conditions
 * - Memory footprint estimation accuracy
 * - Runtime scaling with extreme values
 * - Auto-detection at memory boundaries
 * - Global configuration management
 * - Edge cases and error handling
 */

#include <ipb/common/memory_config.hpp>
#include <ipb/common/platform.hpp>

#include <limits>

#include <gtest/gtest.h>

using namespace ipb::common;

// ============================================================================
// Memory Profile Factory Tests
// ============================================================================

class MemoryProfileTest : public ::testing::Test {};

TEST_F(MemoryProfileTest, EmbeddedProfileHasMinimalSettings) {
    auto config = MemoryConfig::embedded();

    EXPECT_LE(config.scheduler_max_queue_size, 256u);
    EXPECT_LE(config.message_bus_max_channels, 8u);
    EXPECT_LE(config.message_bus_buffer_size, 256u);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(MemoryProfileTest, IoTProfileHasConstrainedSettings) {
    auto config = MemoryConfig::iot();

    EXPECT_LE(config.scheduler_max_queue_size, 1000u);
    EXPECT_LE(config.message_bus_max_channels, 16u);
    EXPECT_LE(config.message_bus_buffer_size, 1024u);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(MemoryProfileTest, EdgeProfileHasBalancedSettings) {
    auto config = MemoryConfig::edge();

    EXPECT_LE(config.scheduler_max_queue_size, 5000u);
    EXPECT_LE(config.message_bus_max_channels, 32u);
    EXPECT_LE(config.message_bus_buffer_size, 2048u);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(MemoryProfileTest, StandardProfileIsDefault) {
    auto config = MemoryConfig::standard();

    EXPECT_EQ(config.scheduler_max_queue_size, 10000u);
    EXPECT_EQ(config.message_bus_max_channels, 64u);
    EXPECT_EQ(config.message_bus_buffer_size, 4096u);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(MemoryProfileTest, HighPerfProfileHasMaxSettings) {
    auto config = MemoryConfig::high_performance();

    EXPECT_GE(config.scheduler_max_queue_size, 50000u);
    EXPECT_GE(config.message_bus_max_channels, 256u);
    EXPECT_GE(config.message_bus_buffer_size, 16384u);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(MemoryProfileTest, FromProfileReturnsCorrectConfig) {
    auto embedded = MemoryConfig::from_profile(MemoryProfile::EMBEDDED);
    auto standard = MemoryConfig::from_profile(MemoryProfile::STANDARD);

    EXPECT_LT(embedded.scheduler_max_queue_size, standard.scheduler_max_queue_size);
    EXPECT_LT(embedded.message_bus_max_channels, standard.message_bus_max_channels);
}

TEST_F(MemoryProfileTest, AllProfilesAreValid) {
    EXPECT_TRUE(MemoryConfig::embedded().is_valid());
    EXPECT_TRUE(MemoryConfig::iot().is_valid());
    EXPECT_TRUE(MemoryConfig::edge().is_valid());
    EXPECT_TRUE(MemoryConfig::standard().is_valid());
    EXPECT_TRUE(MemoryConfig::high_performance().is_valid());
}

TEST_F(MemoryProfileTest, ProfilesHaveIncreasingFootprint) {
    auto embedded  = MemoryConfig::embedded();
    auto iot       = MemoryConfig::iot();
    auto edge      = MemoryConfig::edge();
    auto standard  = MemoryConfig::standard();
    auto high_perf = MemoryConfig::high_performance();

    EXPECT_LT(embedded.estimated_footprint(), iot.estimated_footprint());
    EXPECT_LT(iot.estimated_footprint(), edge.estimated_footprint());
    EXPECT_LT(edge.estimated_footprint(), standard.estimated_footprint());
    EXPECT_LT(standard.estimated_footprint(), high_perf.estimated_footprint());
}

TEST_F(MemoryProfileTest, FromProfileCustomReturnsStandard) {
    auto custom   = MemoryConfig::from_profile(MemoryProfile::CUSTOM);
    auto standard = MemoryConfig::standard();

    // CUSTOM should default to STANDARD
    EXPECT_EQ(custom.scheduler_max_queue_size, standard.scheduler_max_queue_size);
}

TEST_F(MemoryProfileTest, FromProfileAutoDetectReturnsStandard) {
    auto auto_detect = MemoryConfig::from_profile(MemoryProfile::AUTO_DETECT);
    auto standard    = MemoryConfig::standard();

    // AUTO_DETECT in from_profile should default to STANDARD
    EXPECT_EQ(auto_detect.scheduler_max_queue_size, standard.scheduler_max_queue_size);
}

// ============================================================================
// Configuration Validation Tests
// ============================================================================

class ConfigValidationTest : public ::testing::Test {};

TEST_F(ConfigValidationTest, ValidConfigPassesValidation) {
    auto config = MemoryConfig::standard();
    EXPECT_TRUE(config.is_valid());
}

TEST_F(ConfigValidationTest, InvalidBufferSizeFailsValidation) {
    MemoryConfig config;
    config.message_bus_buffer_size = 1000;  // Not power of 2
    EXPECT_FALSE(config.is_valid());
}

TEST_F(ConfigValidationTest, ZeroQueueSizeFailsValidation) {
    MemoryConfig config             = MemoryConfig::standard();
    config.scheduler_max_queue_size = 5;  // Less than minimum 10
    EXPECT_FALSE(config.is_valid());
}

TEST_F(ConfigValidationTest, PowerOf2BufferSizesAreValid) {
    MemoryConfig config = MemoryConfig::standard();

    for (size_t size : {64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384}) {
        config.message_bus_buffer_size = size;
        EXPECT_TRUE(config.is_valid()) << "Size " << size << " should be valid";
    }
}

TEST_F(ConfigValidationTest, NonPowerOf2BufferSizesAreInvalid) {
    MemoryConfig config = MemoryConfig::standard();

    for (size_t size : {100, 200, 300, 500, 1000, 2000, 3000, 5000}) {
        config.message_bus_buffer_size = size;
        EXPECT_FALSE(config.is_valid()) << "Size " << size << " should be invalid";
    }
}

TEST_F(ConfigValidationTest, BoundaryQueueSizeValidation) {
    MemoryConfig config = MemoryConfig::standard();

    config.scheduler_max_queue_size = 9;  // Just below minimum
    EXPECT_FALSE(config.is_valid());

    config.scheduler_max_queue_size = 10;  // Exactly minimum
    EXPECT_TRUE(config.is_valid());

    config.scheduler_max_queue_size = 11;  // Just above minimum
    EXPECT_TRUE(config.is_valid());
}

TEST_F(ConfigValidationTest, ZeroChannelsFailsValidation) {
    MemoryConfig config             = MemoryConfig::standard();
    config.message_bus_max_channels = 0;
    EXPECT_FALSE(config.is_valid());
}

TEST_F(ConfigValidationTest, MinimumChannelsPassesValidation) {
    MemoryConfig config             = MemoryConfig::standard();
    config.message_bus_max_channels = 1;
    EXPECT_TRUE(config.is_valid());
}

TEST_F(ConfigValidationTest, BufferSizeBelowMinimumFails) {
    MemoryConfig config            = MemoryConfig::standard();
    config.message_bus_buffer_size = 32;  // Below minimum of 64
    EXPECT_FALSE(config.is_valid());
}

TEST_F(ConfigValidationTest, BufferSizeAtMinimumPasses) {
    MemoryConfig config            = MemoryConfig::standard();
    config.message_bus_buffer_size = 64;  // Exactly minimum
    EXPECT_TRUE(config.is_valid());
}

TEST_F(ConfigValidationTest, VeryLargeBufferSizePasses) {
    MemoryConfig config            = MemoryConfig::standard();
    config.message_bus_buffer_size = 1024 * 1024;  // 1MB, power of 2
    EXPECT_TRUE(config.is_valid());
}

// ============================================================================
// Memory Footprint Estimation Tests
// ============================================================================

class FootprintTest : public ::testing::Test {};

TEST_F(FootprintTest, EmbeddedHasSmallestFootprint) {
    auto embedded  = MemoryConfig::embedded();
    auto standard  = MemoryConfig::standard();
    auto high_perf = MemoryConfig::high_performance();

    EXPECT_LT(embedded.estimated_footprint(), standard.estimated_footprint());
    EXPECT_LT(standard.estimated_footprint(), high_perf.estimated_footprint());
}

TEST_F(FootprintTest, EmbeddedFootprintUnder50MB) {
    auto config = MemoryConfig::embedded();
    EXPECT_LT(config.estimated_footprint_mb(), 50u);
}

TEST_F(FootprintTest, StandardFootprintUnder500MB) {
    auto config = MemoryConfig::standard();
    EXPECT_LT(config.estimated_footprint_mb(), 500u);
}

TEST_F(FootprintTest, FootprintIsPositive) {
    auto config = MemoryConfig::standard();
    EXPECT_GT(config.estimated_footprint(), 0u);
    EXPECT_GT(config.estimated_footprint_mb(), 0u);
}

TEST_F(FootprintTest, FootprintIncreasesWithQueueSize) {
    MemoryConfig config1             = MemoryConfig::standard();
    MemoryConfig config2             = MemoryConfig::standard();
    config2.scheduler_max_queue_size = config1.scheduler_max_queue_size * 2;

    EXPECT_GT(config2.estimated_footprint(), config1.estimated_footprint());
}

TEST_F(FootprintTest, FootprintIncreasesWithChannels) {
    MemoryConfig config1             = MemoryConfig::standard();
    MemoryConfig config2             = MemoryConfig::standard();
    config2.message_bus_max_channels = config1.message_bus_max_channels * 2;

    EXPECT_GT(config2.estimated_footprint(), config1.estimated_footprint());
}

TEST_F(FootprintTest, FootprintIncreasesWithBufferSize) {
    MemoryConfig config1            = MemoryConfig::standard();
    MemoryConfig config2            = MemoryConfig::standard();
    config2.message_bus_buffer_size = config1.message_bus_buffer_size * 2;

    EXPECT_GT(config2.estimated_footprint(), config1.estimated_footprint());
}

TEST_F(FootprintTest, MinimalConfigFootprint) {
    MemoryConfig config;
    config.scheduler_max_queue_size = 10;
    config.message_bus_max_channels = 1;
    config.message_bus_buffer_size  = 64;
    config.pool_small_capacity      = 0;
    config.pool_medium_capacity     = 0;
    config.pool_large_capacity      = 0;

    // Should be very small but positive
    EXPECT_GT(config.estimated_footprint(), 0u);
    EXPECT_LT(config.estimated_footprint_mb(), 10u);
}

TEST_F(FootprintTest, FootprintMBConsistentWithBytes) {
    auto config  = MemoryConfig::standard();
    size_t bytes = config.estimated_footprint();
    size_t mb    = config.estimated_footprint_mb();

    EXPECT_EQ(mb, bytes / (1024 * 1024));
}

// ============================================================================
// Memory Scaling Tests
// ============================================================================

class ScalingTest : public ::testing::Test {};

TEST_F(ScalingTest, ScalingReducesFootprint) {
    auto config = MemoryConfig::high_performance();
    auto scaled = config.scaled_to(100);  // Scale to 100MB

    EXPECT_LE(scaled.estimated_footprint_mb(), 100u + 10u);  // Allow some margin
    EXPECT_TRUE(scaled.is_valid());
}

TEST_F(ScalingTest, ScalingPreservesMinimums) {
    auto config = MemoryConfig::standard();
    auto scaled = config.scaled_to(1);  // Scale to 1MB (very aggressive)

    // Should still have minimum functional values
    EXPECT_GE(scaled.scheduler_max_queue_size, 100u);
    EXPECT_GE(scaled.message_bus_max_channels, 4u);
    EXPECT_GE(scaled.message_bus_buffer_size, 256u);
    EXPECT_TRUE(scaled.is_valid());
}

TEST_F(ScalingTest, NoScalingIfUnderLimit) {
    auto config = MemoryConfig::embedded();
    auto scaled = config.scaled_to(1000);  // Much larger than needed

    EXPECT_EQ(config.scheduler_max_queue_size, scaled.scheduler_max_queue_size);
    EXPECT_EQ(config.message_bus_buffer_size, scaled.message_bus_buffer_size);
}

TEST_F(ScalingTest, ZeroLimitReturnsUnchanged) {
    auto config = MemoryConfig::standard();
    auto scaled = config.scaled_to(0);

    EXPECT_EQ(config.scheduler_max_queue_size, scaled.scheduler_max_queue_size);
}

TEST_F(ScalingTest, ScaledConfigIsValid) {
    auto config = MemoryConfig::high_performance();

    // Test various scaling targets
    for (size_t target : {10, 50, 100, 200, 500}) {
        auto scaled = config.scaled_to(target);
        EXPECT_TRUE(scaled.is_valid()) << "Scaled to " << target << "MB should be valid";
    }
}

TEST_F(ScalingTest, AggressiveScalingStillValid) {
    auto config = MemoryConfig::high_performance();
    auto scaled = config.scaled_to(1);  // Extremely aggressive

    // Must maintain minimum values for functionality
    EXPECT_TRUE(scaled.is_valid());
    EXPECT_GE(scaled.scheduler_max_queue_size, 10u);  // Min from is_valid()
    EXPECT_GE(scaled.message_bus_max_channels, 1u);   // Min from is_valid()
    EXPECT_GE(scaled.message_bus_buffer_size, 64u);   // Min from is_valid()
}

TEST_F(ScalingTest, ScalingMaintainsPowerOf2BufferSize) {
    auto config = MemoryConfig::high_performance();
    auto scaled = config.scaled_to(50);

    // Buffer size must always be power of 2
    size_t buf_size = scaled.message_bus_buffer_size;
    EXPECT_TRUE((buf_size & (buf_size - 1)) == 0) << "Buffer size must be power of 2";
}

TEST_F(ScalingTest, ScalingMultipleTimes) {
    auto config = MemoryConfig::high_performance();

    // Scale down progressively
    auto scaled1 = config.scaled_to(500);
    auto scaled2 = scaled1.scaled_to(100);
    auto scaled3 = scaled2.scaled_to(50);

    EXPECT_TRUE(scaled1.is_valid());
    EXPECT_TRUE(scaled2.is_valid());
    EXPECT_TRUE(scaled3.is_valid());

    EXPECT_GE(scaled1.estimated_footprint(), scaled2.estimated_footprint());
    EXPECT_GE(scaled2.estimated_footprint(), scaled3.estimated_footprint());
}

TEST_F(ScalingTest, ScalingAllPoolCapacities) {
    auto config = MemoryConfig::high_performance();
    auto scaled = config.scaled_to(10);

    // All pool capacities should be reduced
    EXPECT_LE(scaled.pool_small_capacity, config.pool_small_capacity);
    EXPECT_LE(scaled.pool_medium_capacity, config.pool_medium_capacity);
    EXPECT_LE(scaled.pool_large_capacity, config.pool_large_capacity);
}

// ============================================================================
// Auto-Detection Tests
// ============================================================================

class AutoDetectTest : public ::testing::Test {};

TEST_F(AutoDetectTest, CreateForMemoryReturnsValidConfig) {
    auto config = MemoryConfig::create_for_memory(1024 * 1024 * 1024);  // 1GB
    EXPECT_TRUE(config.is_valid());
}

TEST_F(AutoDetectTest, LowMemoryGetsEmbeddedProfile) {
    auto config = MemoryConfig::create_for_memory(32 * 1024 * 1024);  // 32MB
    EXPECT_EQ(config.scheduler_max_queue_size, MemoryConfig::embedded().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, HighMemoryGetsHighPerfProfile) {
    auto config = MemoryConfig::create_for_memory(16ULL * 1024 * 1024 * 1024);  // 16GB
    EXPECT_EQ(config.scheduler_max_queue_size,
              MemoryConfig::high_performance().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, AutoDetectReturnsValidConfig) {
    auto config = MemoryConfig::auto_detect();
    EXPECT_TRUE(config.is_valid());
}

// Boundary tests for memory detection
TEST_F(AutoDetectTest, MemoryBoundary64MB) {
    constexpr uint64_t MB = 1024ULL * 1024ULL;

    auto below = MemoryConfig::create_for_memory(63 * MB);
    auto at    = MemoryConfig::create_for_memory(64 * MB);

    EXPECT_EQ(below.scheduler_max_queue_size, MemoryConfig::embedded().scheduler_max_queue_size);
    EXPECT_EQ(at.scheduler_max_queue_size, MemoryConfig::iot().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, MemoryBoundary256MB) {
    constexpr uint64_t MB = 1024ULL * 1024ULL;

    auto below = MemoryConfig::create_for_memory(255 * MB);
    auto at    = MemoryConfig::create_for_memory(256 * MB);

    EXPECT_EQ(below.scheduler_max_queue_size, MemoryConfig::iot().scheduler_max_queue_size);
    EXPECT_EQ(at.scheduler_max_queue_size, MemoryConfig::edge().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, MemoryBoundary1GB) {
    constexpr uint64_t MB = 1024ULL * 1024ULL;
    constexpr uint64_t GB = 1024ULL * MB;

    auto below = MemoryConfig::create_for_memory(GB - 1);
    auto at    = MemoryConfig::create_for_memory(GB);

    EXPECT_EQ(below.scheduler_max_queue_size, MemoryConfig::edge().scheduler_max_queue_size);
    EXPECT_EQ(at.scheduler_max_queue_size, MemoryConfig::standard().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, MemoryBoundary8GB) {
    constexpr uint64_t MB = 1024ULL * 1024ULL;
    constexpr uint64_t GB = 1024ULL * MB;

    auto below = MemoryConfig::create_for_memory(8 * GB - 1);
    auto at    = MemoryConfig::create_for_memory(8 * GB);

    EXPECT_EQ(below.scheduler_max_queue_size, MemoryConfig::standard().scheduler_max_queue_size);
    EXPECT_EQ(at.scheduler_max_queue_size,
              MemoryConfig::high_performance().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, ZeroMemoryGetsEmbedded) {
    auto config = MemoryConfig::create_for_memory(0);
    EXPECT_EQ(config.scheduler_max_queue_size, MemoryConfig::embedded().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, VeryLargeMemoryGetsHighPerf) {
    auto config = MemoryConfig::create_for_memory(1024ULL * 1024 * 1024 * 1024);  // 1TB
    EXPECT_EQ(config.scheduler_max_queue_size,
              MemoryConfig::high_performance().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, MaxUint64MemoryGetsHighPerf) {
    auto config = MemoryConfig::create_for_memory(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(config.scheduler_max_queue_size,
              MemoryConfig::high_performance().scheduler_max_queue_size);
}

// ============================================================================
// Global Configuration Tests
// ============================================================================

class GlobalConfigTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Reset to default after each test
        GlobalMemoryConfig::set_profile(MemoryProfile::STANDARD);
    }
};

TEST_F(GlobalConfigTest, SetProfileUpdatesInstance) {
    GlobalMemoryConfig::set_profile(MemoryProfile::EMBEDDED);
    auto& config = GlobalMemoryConfig::instance();

    EXPECT_EQ(config.scheduler_max_queue_size, MemoryConfig::embedded().scheduler_max_queue_size);
}

TEST_F(GlobalConfigTest, SetCustomConfigWorks) {
    MemoryConfig custom;
    custom.scheduler_max_queue_size = 12345;
    custom.message_bus_buffer_size  = 1024;
    custom.message_bus_max_channels = 10;

    GlobalMemoryConfig::set(custom);
    auto& config = GlobalMemoryConfig::instance();

    EXPECT_EQ(config.scheduler_max_queue_size, 12345u);
}

TEST_F(GlobalConfigTest, SetMemoryLimitScalesConfig) {
    GlobalMemoryConfig::set_profile(MemoryProfile::HIGH_PERF);
    GlobalMemoryConfig::set_memory_limit(100);  // 100MB

    auto& config = GlobalMemoryConfig::instance();
    EXPECT_LE(config.estimated_footprint_mb(), 110u);  // Allow margin
}

TEST_F(GlobalConfigTest, InstanceIsSingleton) {
    auto& config1 = GlobalMemoryConfig::instance();
    auto& config2 = GlobalMemoryConfig::instance();

    EXPECT_EQ(&config1, &config2);
}

TEST_F(GlobalConfigTest, SetAllProfiles) {
    GlobalMemoryConfig::set_profile(MemoryProfile::EMBEDDED);
    EXPECT_EQ(GlobalMemoryConfig::instance().scheduler_max_queue_size,
              MemoryConfig::embedded().scheduler_max_queue_size);

    GlobalMemoryConfig::set_profile(MemoryProfile::IOT);
    EXPECT_EQ(GlobalMemoryConfig::instance().scheduler_max_queue_size,
              MemoryConfig::iot().scheduler_max_queue_size);

    GlobalMemoryConfig::set_profile(MemoryProfile::EDGE);
    EXPECT_EQ(GlobalMemoryConfig::instance().scheduler_max_queue_size,
              MemoryConfig::edge().scheduler_max_queue_size);

    GlobalMemoryConfig::set_profile(MemoryProfile::STANDARD);
    EXPECT_EQ(GlobalMemoryConfig::instance().scheduler_max_queue_size,
              MemoryConfig::standard().scheduler_max_queue_size);

    GlobalMemoryConfig::set_profile(MemoryProfile::HIGH_PERF);
    EXPECT_EQ(GlobalMemoryConfig::instance().scheduler_max_queue_size,
              MemoryConfig::high_performance().scheduler_max_queue_size);
}

TEST_F(GlobalConfigTest, SetAutoDetectProfile) {
    GlobalMemoryConfig::set_profile(MemoryProfile::AUTO_DETECT);
    auto& config = GlobalMemoryConfig::instance();

    // Should return a valid config (actual values depend on system memory)
    EXPECT_TRUE(config.is_valid());
}

TEST_F(GlobalConfigTest, MultipleMemoryLimitChanges) {
    GlobalMemoryConfig::set_profile(MemoryProfile::HIGH_PERF);

    GlobalMemoryConfig::set_memory_limit(500);
    auto footprint1 = GlobalMemoryConfig::instance().estimated_footprint_mb();

    GlobalMemoryConfig::set_memory_limit(100);
    auto footprint2 = GlobalMemoryConfig::instance().estimated_footprint_mb();

    // Second limit should be smaller or equal (may hit minimums)
    EXPECT_LE(footprint2, footprint1 + 50);  // Allow some margin
}

// ============================================================================
// Default Profile Tests
// ============================================================================

class DefaultProfileTest : public ::testing::Test {};

TEST_F(DefaultProfileTest, GetDefaultConfigReturnsValid) {
    auto config = get_default_memory_config();
    EXPECT_TRUE(config.is_valid());
}

TEST_F(DefaultProfileTest, DefaultProfileIsStandard) {
    // Unless compile-time override, should be STANDARD
    EXPECT_EQ(DEFAULT_MEMORY_PROFILE, MemoryProfile::STANDARD);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

class MemoryConfigEdgeCaseTest : public ::testing::Test {};

TEST_F(MemoryConfigEdgeCaseTest, DefaultConstructedConfig) {
    MemoryConfig config;

    // Default values should be standard
    EXPECT_EQ(config.scheduler_max_queue_size, 10000u);
    EXPECT_EQ(config.message_bus_max_channels, 64u);
    EXPECT_EQ(config.message_bus_buffer_size, 4096u);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(MemoryConfigEdgeCaseTest, CopyConstruction) {
    auto original     = MemoryConfig::embedded();
    MemoryConfig copy = original;

    EXPECT_EQ(copy.scheduler_max_queue_size, original.scheduler_max_queue_size);
    EXPECT_EQ(copy.message_bus_max_channels, original.message_bus_max_channels);
    EXPECT_EQ(copy.message_bus_buffer_size, original.message_bus_buffer_size);
}

TEST_F(MemoryConfigEdgeCaseTest, CopyAssignment) {
    auto original = MemoryConfig::embedded();
    MemoryConfig copy;
    copy = original;

    EXPECT_EQ(copy.scheduler_max_queue_size, original.scheduler_max_queue_size);
}

TEST_F(MemoryConfigEdgeCaseTest, MoveConstruction) {
    auto original         = MemoryConfig::embedded();
    size_t expected_queue = original.scheduler_max_queue_size;
    MemoryConfig moved    = std::move(original);

    EXPECT_EQ(moved.scheduler_max_queue_size, expected_queue);
}

TEST_F(MemoryConfigEdgeCaseTest, ExtremeSchedulerQueueSize) {
    MemoryConfig config             = MemoryConfig::standard();
    config.scheduler_max_queue_size = std::numeric_limits<size_t>::max();

    // Should still be valid as long as buffer size is valid
    EXPECT_TRUE(config.is_valid());
    // Footprint will overflow but that's expected for extreme values
}

TEST_F(MemoryConfigEdgeCaseTest, LargeBufferSizePowerOf2) {
    MemoryConfig config = MemoryConfig::standard();

    // Test large power of 2 values
    config.message_bus_buffer_size = 1ULL << 20;  // 1MB
    EXPECT_TRUE(config.is_valid());

    config.message_bus_buffer_size = 1ULL << 30;  // 1GB
    EXPECT_TRUE(config.is_valid());
}

TEST_F(MemoryConfigEdgeCaseTest, AllFieldsModified) {
    MemoryConfig config;
    config.scheduler_max_queue_size       = 1000;
    config.scheduler_worker_threads       = 4;
    config.message_bus_max_channels       = 32;
    config.message_bus_buffer_size        = 2048;
    config.message_bus_dispatcher_threads = 2;
    config.pool_small_capacity            = 500;
    config.pool_medium_capacity           = 250;
    config.pool_large_capacity            = 125;
    config.pool_block_size                = 32;
    config.router_max_rules               = 128;
    config.router_max_sinks               = 16;
    config.router_batch_size              = 8;
    config.pattern_cache_size             = 64;

    EXPECT_TRUE(config.is_valid());
    EXPECT_GT(config.estimated_footprint(), 0u);
}

TEST_F(MemoryConfigEdgeCaseTest, ZeroPoolCapacities) {
    MemoryConfig config         = MemoryConfig::standard();
    config.pool_small_capacity  = 0;
    config.pool_medium_capacity = 0;
    config.pool_large_capacity  = 0;

    // Should still be valid - pools are optional
    EXPECT_TRUE(config.is_valid());
    EXPECT_GT(config.estimated_footprint(), 0u);
}

TEST_F(MemoryConfigEdgeCaseTest, ConstexprCompileTimeProfile) {
    // Verify constexpr factory methods work at compile time
    constexpr auto embedded  = MemoryConfig::embedded();
    constexpr auto iot       = MemoryConfig::iot();
    constexpr auto edge      = MemoryConfig::edge();
    constexpr auto standard  = MemoryConfig::standard();
    constexpr auto high_perf = MemoryConfig::high_performance();

    // These should compile and have expected values
    static_assert(embedded.scheduler_max_queue_size == 256);
    static_assert(iot.scheduler_max_queue_size == 1000);
    static_assert(edge.scheduler_max_queue_size == 5000);
    static_assert(standard.scheduler_max_queue_size == 10000);
    static_assert(high_perf.scheduler_max_queue_size == 50000);
}

TEST_F(MemoryConfigEdgeCaseTest, ConstexprValidation) {
    // Verify is_valid() works at compile time
    constexpr auto config = MemoryConfig::standard();
    static_assert(config.is_valid());
}

TEST_F(MemoryConfigEdgeCaseTest, ConstexprFootprint) {
    // Verify footprint calculation works at compile time
    constexpr auto config      = MemoryConfig::standard();
    constexpr size_t footprint = config.estimated_footprint();
    static_assert(footprint > 0);
}
