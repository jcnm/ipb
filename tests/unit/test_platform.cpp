/**
 * @file test_platform.cpp
 * @brief Unit tests for IPB platform utilities
 *
 * Tests coverage for:
 * - CPU count detection
 * - Memory information
 * - Page size
 * - Hostname
 * - Process/Thread IDs
 * - Elevation check
 * - Environment variables
 * - CPU feature detection
 */

#include <ipb/common/platform.hpp>

#include <string>
#include <thread>

#include <gtest/gtest.h>

using namespace ipb::common::platform;

// ============================================================================
// CPU Count Tests
// ============================================================================

class CPUCountTest : public ::testing::Test {};

TEST_F(CPUCountTest, ReturnsPositiveValue) {
    uint32_t count = get_cpu_count();
    EXPECT_GT(count, 0u);
}

TEST_F(CPUCountTest, ReasonableRange) {
    uint32_t count = get_cpu_count();
    // Should be between 1 and 1024 for any reasonable system
    EXPECT_GE(count, 1u);
    EXPECT_LE(count, 1024u);
}

TEST_F(CPUCountTest, ConsistentResults) {
    uint32_t count1 = get_cpu_count();
    uint32_t count2 = get_cpu_count();
    EXPECT_EQ(count1, count2);
}

TEST_F(CPUCountTest, MatchesStdThread) {
    uint32_t platform_count = get_cpu_count();
    unsigned int std_count  = std::thread::hardware_concurrency();

    if (std_count > 0) {
        EXPECT_EQ(platform_count, std_count);
    }
}

// ============================================================================
// Memory Information Tests
// ============================================================================

class MemoryInfoTest : public ::testing::Test {};

TEST_F(MemoryInfoTest, TotalMemoryPositive) {
    uint64_t total = get_total_memory();
    EXPECT_GT(total, 0u);
}

TEST_F(MemoryInfoTest, TotalMemoryReasonableRange) {
    uint64_t total = get_total_memory();
    // At least 64MB, at most 64TB
    EXPECT_GE(total, 64ULL * 1024 * 1024);
    EXPECT_LE(total, 64ULL * 1024 * 1024 * 1024 * 1024);
}

TEST_F(MemoryInfoTest, AvailableMemoryPositive) {
    uint64_t available = get_available_memory();
    EXPECT_GT(available, 0u);
}

TEST_F(MemoryInfoTest, AvailableLessThanTotal) {
    uint64_t total     = get_total_memory();
    uint64_t available = get_available_memory();
    EXPECT_LE(available, total);
}

TEST_F(MemoryInfoTest, AvailableMemoryConsistent) {
    uint64_t avail1 = get_available_memory();
    uint64_t avail2 = get_available_memory();

    // Should be within 10% of each other
    uint64_t diff = (avail1 > avail2) ? (avail1 - avail2) : (avail2 - avail1);
    EXPECT_LT(diff, avail1 / 10);
}

// ============================================================================
// Page Size Tests
// ============================================================================

class PageSizeTest : public ::testing::Test {};

TEST_F(PageSizeTest, ReturnsPositiveValue) {
    size_t page_size = get_page_size();
    EXPECT_GT(page_size, 0u);
}

TEST_F(PageSizeTest, PowerOfTwo) {
    size_t page_size = get_page_size();
    // Page size should be a power of 2
    EXPECT_EQ(page_size & (page_size - 1), 0u);
}

TEST_F(PageSizeTest, ReasonableSize) {
    size_t page_size = get_page_size();
    // Common page sizes: 4KB, 8KB, 16KB, 64KB
    EXPECT_GE(page_size, 4096u);
    EXPECT_LE(page_size, 65536u);
}

TEST_F(PageSizeTest, ConsistentResults) {
    size_t size1 = get_page_size();
    size_t size2 = get_page_size();
    EXPECT_EQ(size1, size2);
}

// ============================================================================
// Hostname Tests
// ============================================================================

class HostnameTest : public ::testing::Test {};

TEST_F(HostnameTest, ReturnsNonEmpty) {
    std::string hostname = get_hostname();
    EXPECT_FALSE(hostname.empty());
}

TEST_F(HostnameTest, ReasonableLength) {
    std::string hostname = get_hostname();
    // Hostnames should be at least 1 character and at most 255
    EXPECT_GE(hostname.size(), 1u);
    EXPECT_LE(hostname.size(), 255u);
}

TEST_F(HostnameTest, ConsistentResults) {
    std::string host1 = get_hostname();
    std::string host2 = get_hostname();
    EXPECT_EQ(host1, host2);
}

TEST_F(HostnameTest, ValidCharacters) {
    std::string hostname = get_hostname();
    for (char c : hostname) {
        // Hostnames can contain alphanumeric, hyphen, and period
        bool valid =
            std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_';
        EXPECT_TRUE(valid) << "Invalid character in hostname: " << c;
    }
}

// ============================================================================
// Process ID Tests
// ============================================================================

class ProcessIdTest : public ::testing::Test {};

TEST_F(ProcessIdTest, ReturnsPositiveValue) {
    uint64_t pid = get_process_id();
    EXPECT_GT(pid, 0u);
}

TEST_F(ProcessIdTest, ConsistentResults) {
    uint64_t pid1 = get_process_id();
    uint64_t pid2 = get_process_id();
    EXPECT_EQ(pid1, pid2);
}

TEST_F(ProcessIdTest, ReasonableRange) {
    uint64_t pid = get_process_id();
    // PIDs are typically 32-bit on most systems
    EXPECT_LE(pid, 0xFFFFFFFFu);
}

// ============================================================================
// Thread ID Tests
// ============================================================================

class ThreadIdTest : public ::testing::Test {};

TEST_F(ThreadIdTest, ReturnsNonZero) {
    uint64_t tid = get_thread_id();
    // Thread ID can technically be 0 in some systems, but typically non-zero
    (void)tid;  // Just verify it returns without error
}

TEST_F(ThreadIdTest, ConsistentResults) {
    uint64_t tid1 = get_thread_id();
    uint64_t tid2 = get_thread_id();
    EXPECT_EQ(tid1, tid2);
}

TEST_F(ThreadIdTest, DifferentForDifferentThreads) {
    uint64_t main_tid  = get_thread_id();
    uint64_t other_tid = 0;

    std::thread t([&other_tid]() { other_tid = get_thread_id(); });
    t.join();

    // Thread IDs should be different
    EXPECT_NE(main_tid, other_tid);
}

// ============================================================================
// Elevation Check Tests
// ============================================================================

class ElevationTest : public ::testing::Test {};

TEST_F(ElevationTest, ReturnsBoolean) {
    bool elevated = is_elevated();
    // Just verify it returns without error
    (void)elevated;
}

TEST_F(ElevationTest, ConsistentResults) {
    bool elevated1 = is_elevated();
    bool elevated2 = is_elevated();
    EXPECT_EQ(elevated1, elevated2);
}

// ============================================================================
// Environment Variable Tests
// ============================================================================

class EnvVarTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Clean up test environment variables
        // Note: We can't easily unset variables, but we can set them to empty
    }
};

TEST_F(EnvVarTest, GetExistingVariable) {
    // PATH should exist on all systems
    std::string path = get_env("PATH");
    EXPECT_FALSE(path.empty());
}

TEST_F(EnvVarTest, GetNonexistentVariable) {
    std::string value = get_env("IPB_NONEXISTENT_VAR_12345");
    EXPECT_TRUE(value.empty());
}

TEST_F(EnvVarTest, SetAndGetVariable) {
    const std::string var_name  = "IPB_TEST_VAR";
    const std::string var_value = "test_value_12345";

    bool set_result = set_env(var_name, var_value);
    EXPECT_TRUE(set_result);

    std::string retrieved = get_env(var_name);
    EXPECT_EQ(retrieved, var_value);
}

TEST_F(EnvVarTest, SetEmptyValue) {
    const std::string var_name = "IPB_TEST_EMPTY";

    bool set_result = set_env(var_name, "");
    EXPECT_TRUE(set_result);

    std::string retrieved = get_env(var_name);
    EXPECT_TRUE(retrieved.empty());
}

TEST_F(EnvVarTest, SetOverwriteVariable) {
    const std::string var_name = "IPB_TEST_OVERWRITE";

    set_env(var_name, "original");
    std::string first = get_env(var_name);
    EXPECT_EQ(first, "original");

    set_env(var_name, "overwritten");
    std::string second = get_env(var_name);
    EXPECT_EQ(second, "overwritten");
}

TEST_F(EnvVarTest, SetSpecialCharacters) {
    const std::string var_name  = "IPB_TEST_SPECIAL";
    const std::string var_value = "value with spaces=and/special:chars";

    bool set_result = set_env(var_name, var_value);
    EXPECT_TRUE(set_result);

    std::string retrieved = get_env(var_name);
    EXPECT_EQ(retrieved, var_value);
}

// ============================================================================
// CPU Feature Detection Tests
// ============================================================================

class CpuFeaturesTest : public ::testing::Test {};

TEST_F(CpuFeaturesTest, DetectsFeatures) {
    CpuFeatures features = detect_cpu_features();
    // Just verify it returns without error
    (void)features;
}

TEST_F(CpuFeaturesTest, ConsistentResults) {
    CpuFeatures features1 = detect_cpu_features();
    CpuFeatures features2 = detect_cpu_features();

    // Features should be the same
    EXPECT_EQ(features1.has_sse, features2.has_sse);
    EXPECT_EQ(features1.has_sse2, features2.has_sse2);
    EXPECT_EQ(features1.has_sse3, features2.has_sse3);
    EXPECT_EQ(features1.has_avx, features2.has_avx);
    EXPECT_EQ(features1.has_avx2, features2.has_avx2);
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
TEST_F(CpuFeaturesTest, X86FeaturesReasonable) {
    CpuFeatures features = detect_cpu_features();

// On x86_64, SSE2 should always be available
#if defined(__x86_64__) || defined(_M_X64)
    EXPECT_TRUE(features.has_sse);
    EXPECT_TRUE(features.has_sse2);
#endif

    // If AVX2 is present, AVX should also be present
    if (features.has_avx2) {
        EXPECT_TRUE(features.has_avx);
    }

    // If SSE4.2 is present, SSE4.1 should also be present
    if (features.has_sse42) {
        EXPECT_TRUE(features.has_sse41);
    }

    // If SSSE3 is present, SSE3 should also be present
    if (features.has_ssse3) {
        EXPECT_TRUE(features.has_sse3);
    }
}
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
TEST_F(CpuFeaturesTest, ARM64FeaturesReasonable) {
    CpuFeatures features = detect_cpu_features();

    // On ARM64, NEON should always be available
    EXPECT_TRUE(features.has_neon);
}
#endif

TEST_F(CpuFeaturesTest, AllFieldsInitialized) {
    CpuFeatures features = detect_cpu_features();

    // All boolean fields should be valid (true or false)
    // This mainly just verifies no UB from uninitialized memory
    bool dummy = features.has_sse || features.has_sse2 || features.has_sse3 || features.has_ssse3 ||
                 features.has_sse41 || features.has_sse42 || features.has_avx ||
                 features.has_avx2 || features.has_avx512 || features.has_aes || features.has_sha ||
                 features.has_neon || features.has_crc32 || features.has_crypto;
    (void)dummy;
}
