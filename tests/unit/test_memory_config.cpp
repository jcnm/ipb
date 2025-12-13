/**
 * @file test_memory_config.cpp
 * @brief Unit tests for IPB memory configuration system
 *
 * Tests coverage for:
 * - Memory profile presets (EMBEDDED, IOT, EDGE, STANDARD, HIGH_PERF)
 * - Configuration validation
 * - Memory footprint estimation
 * - Runtime scaling
 * - Auto-detection
 * - Global configuration management
 */

#include <ipb/common/memory_config.hpp>
#include <ipb/common/platform.hpp>

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
    MemoryConfig config = MemoryConfig::standard();
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

// ============================================================================
// Memory Footprint Estimation Tests
// ============================================================================

class FootprintTest : public ::testing::Test {};

TEST_F(FootprintTest, EmbeddedHasSmallestFootprint) {
    auto embedded = MemoryConfig::embedded();
    auto standard = MemoryConfig::standard();
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
    size_t original_footprint = config.estimated_footprint_mb();
    auto scaled = config.scaled_to(1000);  // Much larger than needed

    EXPECT_EQ(config.scheduler_max_queue_size, scaled.scheduler_max_queue_size);
    EXPECT_EQ(config.message_bus_buffer_size, scaled.message_bus_buffer_size);
}

TEST_F(ScalingTest, ZeroLimitReturnsUnchanged) {
    auto config = MemoryConfig::standard();
    auto scaled = config.scaled_to(0);

    EXPECT_EQ(config.scheduler_max_queue_size, scaled.scheduler_max_queue_size);
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
    EXPECT_EQ(config.scheduler_max_queue_size, MemoryConfig::high_performance().scheduler_max_queue_size);
}

TEST_F(AutoDetectTest, AutoDetectReturnsValidConfig) {
    auto config = MemoryConfig::auto_detect();
    EXPECT_TRUE(config.is_valid());
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
    custom.message_bus_buffer_size = 1024;
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
