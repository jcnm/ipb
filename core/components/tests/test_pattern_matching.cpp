/**
 * @file test_pattern_matching.cpp
 * @brief Comprehensive tests for enterprise-grade pattern matching components
 *
 * Tests cover:
 * - PatternValidator (ReDoS detection)
 * - CompiledPatternCache (thread-safe caching)
 * - TrieMatcher (O(m) lookup)
 * - FastPatternMatcher (composite matching)
 * - CachedPatternMatcher (RAII helper)
 */

#include <gtest/gtest.h>
#include <ipb/core/rule_engine/pattern_matcher.hpp>
#include <ipb/core/rule_engine/compiled_pattern_cache.hpp>

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace ipb::core;

// ============================================================================
// PatternValidator Tests - ReDoS Detection
// ============================================================================

class PatternValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(PatternValidatorTest, SafePatternsPass) {
    // Simple patterns should be safe
    EXPECT_TRUE(PatternValidator::validate("hello").is_safe);
    EXPECT_TRUE(PatternValidator::validate("sensors/temp1").is_safe);
    EXPECT_TRUE(PatternValidator::validate("ns=2;s=MyNode").is_safe);
    EXPECT_TRUE(PatternValidator::validate("[a-z]+").is_safe);
    EXPECT_TRUE(PatternValidator::validate("\\d{4}-\\d{2}-\\d{2}").is_safe);
}

TEST_F(PatternValidatorTest, NestedQuantifiersDetected) {
    // Classic ReDoS patterns should be detected
    auto result = PatternValidator::validate("(a+)+");
    EXPECT_FALSE(result.is_safe);
    EXPECT_TRUE(result.has_nested_quantifiers);

    result = PatternValidator::validate("(a*)*");
    EXPECT_FALSE(result.is_safe);

    result = PatternValidator::validate("([a-zA-Z]+)*");
    EXPECT_FALSE(result.is_safe);
}

TEST_F(PatternValidatorTest, ComplexityScoring) {
    // Simple pattern has low complexity
    auto simple = PatternValidator::calculate_complexity("hello");
    EXPECT_LT(simple, 5u);

    // Pattern with quantifiers has higher complexity
    auto with_quants = PatternValidator::calculate_complexity("[a-z]+.*\\d*");
    EXPECT_GT(with_quants, simple);

    // Pattern with groups has even higher complexity
    auto with_groups = PatternValidator::calculate_complexity("(\\d+)-(\\d+)");
    EXPECT_GT(with_groups, simple);
}

TEST_F(PatternValidatorTest, EmptyPatternRejected) {
    auto result = PatternValidator::validate("");
    EXPECT_FALSE(result.is_safe);
}

TEST_F(PatternValidatorTest, BackreferencesDetected) {
    auto result = PatternValidator::validate("(a)\\1+");
    EXPECT_TRUE(result.has_backreferences);
}

// ============================================================================
// CompiledPatternCache Tests
// ============================================================================

class CompiledPatternCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache_ = std::make_unique<CompiledPatternCache>();
    }

    std::unique_ptr<CompiledPatternCache> cache_;
};

TEST_F(CompiledPatternCacheTest, BasicCompilation) {
    auto result = cache_->get_or_compile("hello");
    ASSERT_TRUE(result);
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(CompiledPatternCacheTest, CacheHit) {
    // First compilation
    auto result1 = cache_->get_or_compile("sensors/.*");
    ASSERT_TRUE(result1);

    auto stats_before = cache_->stats();

    // Second access should be cache hit
    auto result2 = cache_->get_or_compile("sensors/.*");
    ASSERT_TRUE(result2);

    // Same pointer (cached)
    EXPECT_EQ(result1.value(), result2.value());

    auto stats_after = cache_->stats();
    EXPECT_GT(stats_after.cache_hits, stats_before.cache_hits);
}

TEST_F(CompiledPatternCacheTest, InvalidPatternRejected) {
    auto result = cache_->get_or_compile("[invalid(");
    EXPECT_FALSE(result);
}

TEST_F(CompiledPatternCacheTest, DangerousPatternRejected) {
    // ReDoS pattern should be rejected
    auto result = cache_->get_or_compile("(a+)+");
    EXPECT_FALSE(result);

    auto stats = cache_->stats();
    EXPECT_GT(stats.validation_rejections, 0u);
}

TEST_F(CompiledPatternCacheTest, Precompile) {
    auto result = cache_->precompile("\\d{4}");
    EXPECT_TRUE(result);
    EXPECT_TRUE(cache_->contains("\\d{4}"));
}

TEST_F(CompiledPatternCacheTest, Remove) {
    cache_->precompile("test_pattern");
    EXPECT_TRUE(cache_->contains("test_pattern"));

    cache_->remove("test_pattern");
    EXPECT_FALSE(cache_->contains("test_pattern"));
}

TEST_F(CompiledPatternCacheTest, Clear) {
    cache_->precompile("pattern1");
    cache_->precompile("pattern2");
    EXPECT_EQ(cache_->size(), 2u);

    cache_->clear();
    EXPECT_EQ(cache_->size(), 0u);
}

TEST_F(CompiledPatternCacheTest, ThreadSafety) {
    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, t, &success_count]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                std::string pattern = "pattern_" + std::to_string(t) + "_" + std::to_string(i % 10);
                auto result = cache_->get_or_compile(pattern);
                if (result) {
                    success_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All operations should succeed
    EXPECT_EQ(success_count.load(), NUM_THREADS * OPS_PER_THREAD);
}

// ============================================================================
// TrieMatcher Tests
// ============================================================================

class TrieMatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        trie_ = std::make_unique<TrieMatcher>();
    }

    std::unique_ptr<TrieMatcher> trie_;
};

TEST_F(TrieMatcherTest, ExactMatch) {
    trie_->add_exact("sensors/temp1", 1);
    trie_->add_exact("sensors/temp2", 2);

    auto exact = trie_->find_exact("sensors/temp1");
    ASSERT_TRUE(exact.has_value());
    EXPECT_EQ(*exact, 1u);

    exact = trie_->find_exact("sensors/temp2");
    ASSERT_TRUE(exact.has_value());
    EXPECT_EQ(*exact, 2u);

    // No match for different address
    exact = trie_->find_exact("sensors/temp3");
    EXPECT_FALSE(exact.has_value());
}

TEST_F(TrieMatcherTest, PrefixMatch) {
    trie_->add_prefix("sensors/", 10);
    trie_->add_prefix("alarms/", 20);

    auto matches = trie_->find_matches("sensors/temp1");
    EXPECT_FALSE(matches.empty());
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 10u) != matches.end());

    matches = trie_->find_matches("alarms/critical/pump1");
    EXPECT_FALSE(matches.empty());
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 20u) != matches.end());

    // No match for other prefix
    matches = trie_->find_matches("events/log1");
    EXPECT_TRUE(matches.empty());
}

TEST_F(TrieMatcherTest, CombinedExactAndPrefix) {
    trie_->add_exact("sensors/temp1", 1);
    trie_->add_prefix("sensors/", 10);

    auto matches = trie_->find_matches("sensors/temp1");
    EXPECT_EQ(matches.size(), 2u);

    // Exact match should be first (higher priority)
    EXPECT_EQ(matches[0], 1u);
    EXPECT_EQ(matches[1], 10u);
}

TEST_F(TrieMatcherTest, HasMatch) {
    trie_->add_exact("test", 1);

    EXPECT_TRUE(trie_->matches("test"));
    EXPECT_FALSE(trie_->matches("other"));
}

TEST_F(TrieMatcherTest, Remove) {
    trie_->add_exact("test", 1);
    EXPECT_TRUE(trie_->matches("test"));

    trie_->remove("test");
    EXPECT_FALSE(trie_->matches("test"));
}

TEST_F(TrieMatcherTest, Clear) {
    trie_->add_exact("a", 1);
    trie_->add_exact("b", 2);
    trie_->add_prefix("c", 3);

    EXPECT_EQ(trie_->size(), 3u);

    trie_->clear();
    EXPECT_EQ(trie_->size(), 0u);
    EXPECT_TRUE(trie_->empty());
}

TEST_F(TrieMatcherTest, Stats) {
    trie_->add_exact("hello", 1);
    trie_->add_exact("world", 2);

    auto stats = trie_->stats();
    EXPECT_EQ(stats.pattern_count, 2u);
    EXPECT_GT(stats.node_count, 0u);
    EXPECT_GT(stats.memory_bytes, 0u);
}

TEST_F(TrieMatcherTest, LargeScalePerformance) {
    // Add 1000 patterns
    for (int i = 0; i < 1000; ++i) {
        std::string pattern = "sensors/area" + std::to_string(i / 100) +
                             "/device" + std::to_string(i);
        trie_->add_exact(pattern, static_cast<uint32_t>(i));
    }

    EXPECT_EQ(trie_->size(), 1000u);

    // Lookup should be fast (O(m))
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
        std::string pattern = "sensors/area5/device500";
        auto result = trie_->find_exact(pattern);
        EXPECT_TRUE(result.has_value());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // 10000 lookups should complete in reasonable time (< 100ms)
    EXPECT_LT(duration.count(), 100000);
}

// ============================================================================
// FastPatternMatcher Tests
// ============================================================================

class FastPatternMatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        matcher_ = std::make_unique<FastPatternMatcher>();
    }

    std::unique_ptr<FastPatternMatcher> matcher_;
};

TEST_F(FastPatternMatcherTest, AutoDetectExact) {
    auto type = FastPatternMatcher::detect_type("sensors/temp1");
    EXPECT_EQ(type, FastPatternMatcher::PatternType::EXACT);
}

TEST_F(FastPatternMatcherTest, AutoDetectPrefix) {
    auto type = FastPatternMatcher::detect_type("sensors/*");
    EXPECT_EQ(type, FastPatternMatcher::PatternType::PREFIX);
}

TEST_F(FastPatternMatcherTest, AutoDetectWildcard) {
    auto type = FastPatternMatcher::detect_type("sensors/*/temp?");
    EXPECT_EQ(type, FastPatternMatcher::PatternType::WILDCARD);
}

TEST_F(FastPatternMatcherTest, AutoDetectRegex) {
    auto type = FastPatternMatcher::detect_type("sensors/[a-z]+/temp\\d+");
    EXPECT_EQ(type, FastPatternMatcher::PatternType::REGEX);
}

TEST_F(FastPatternMatcherTest, AddExactPattern) {
    EXPECT_TRUE(matcher_->add_pattern("test", 1, FastPatternMatcher::PatternType::EXACT));

    auto matches = matcher_->find_all_matches("test");
    EXPECT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], 1u);
}

TEST_F(FastPatternMatcherTest, AddPrefixPattern) {
    EXPECT_TRUE(matcher_->add_pattern("sensors/*", 1, FastPatternMatcher::PatternType::PREFIX));

    auto matches = matcher_->find_all_matches("sensors/temp1");
    EXPECT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], 1u);
}

TEST_F(FastPatternMatcherTest, AddRegexPattern) {
    EXPECT_TRUE(matcher_->add_pattern("sensors/[a-z]+", 1, FastPatternMatcher::PatternType::REGEX));

    auto matches = matcher_->find_all_matches("sensors/temp");
    EXPECT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], 1u);
}

TEST_F(FastPatternMatcherTest, InvalidRegexRejected) {
    EXPECT_FALSE(matcher_->add_pattern("[invalid(", 1, FastPatternMatcher::PatternType::REGEX));
}

TEST_F(FastPatternMatcherTest, DangerousPatternRejected) {
    // ReDoS pattern should be rejected
    EXPECT_FALSE(matcher_->add_pattern("(a+)+", 1, FastPatternMatcher::PatternType::REGEX));
}

TEST_F(FastPatternMatcherTest, MultiplePatternTypes) {
    matcher_->add_pattern("sensors/temp1", 1, FastPatternMatcher::PatternType::EXACT);
    matcher_->add_pattern("sensors/*", 2, FastPatternMatcher::PatternType::PREFIX);
    matcher_->add_pattern("sensors/[a-z]+\\d", 3, FastPatternMatcher::PatternType::REGEX);

    auto matches = matcher_->find_all_matches("sensors/temp1");

    // Should match exact and prefix
    EXPECT_GE(matches.size(), 2u);
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 1u) != matches.end());
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 2u) != matches.end());
}

TEST_F(FastPatternMatcherTest, HasMatch) {
    matcher_->add_pattern("test", 1);

    EXPECT_TRUE(matcher_->has_match("test"));
    EXPECT_FALSE(matcher_->has_match("other"));
}

TEST_F(FastPatternMatcherTest, RemovePattern) {
    matcher_->add_pattern("test", 1);
    EXPECT_TRUE(matcher_->has_match("test"));

    matcher_->remove_pattern("test");
    EXPECT_FALSE(matcher_->has_match("test"));
}

TEST_F(FastPatternMatcherTest, Clear) {
    matcher_->add_pattern("a", 1);
    matcher_->add_pattern("b", 2);

    auto stats = matcher_->stats();
    EXPECT_GT(stats.exact_patterns + stats.prefix_patterns + stats.wildcard_patterns + stats.regex_patterns, 0u);

    matcher_->clear();

    stats = matcher_->stats();
    EXPECT_EQ(stats.exact_patterns, 0u);
    EXPECT_EQ(stats.prefix_patterns, 0u);
    EXPECT_EQ(stats.wildcard_patterns, 0u);
    EXPECT_EQ(stats.regex_patterns, 0u);
}

TEST_F(FastPatternMatcherTest, Stats) {
    matcher_->add_pattern("exact", 1, FastPatternMatcher::PatternType::EXACT);
    matcher_->add_pattern("prefix*", 2, FastPatternMatcher::PatternType::PREFIX);
    matcher_->add_pattern("wild?card", 3, FastPatternMatcher::PatternType::WILDCARD);
    matcher_->add_pattern("[a-z]+", 4, FastPatternMatcher::PatternType::REGEX);

    auto stats = matcher_->stats();
    EXPECT_EQ(stats.exact_patterns, 1u);
    EXPECT_EQ(stats.prefix_patterns, 1u);
    EXPECT_EQ(stats.wildcard_patterns, 1u);
    EXPECT_EQ(stats.regex_patterns, 1u);
}

// ============================================================================
// CachedPatternMatcher Tests
// ============================================================================

class CachedPatternMatcherTest : public ::testing::Test {};

TEST_F(CachedPatternMatcherTest, ValidPattern) {
    CachedPatternMatcher matcher("hello.*");

    EXPECT_TRUE(matcher.is_valid());
    EXPECT_TRUE(matcher.error().empty());
    EXPECT_TRUE(matcher.matches("hello_world"));
    EXPECT_FALSE(matcher.matches("world_hello"));
}

TEST_F(CachedPatternMatcherTest, InvalidPattern) {
    CachedPatternMatcher matcher("[invalid(");

    EXPECT_FALSE(matcher.is_valid());
    EXPECT_FALSE(matcher.error().empty());
    EXPECT_FALSE(matcher.matches("anything"));
}

TEST_F(CachedPatternMatcherTest, DangerousPatternRejected) {
    CachedPatternMatcher matcher("(a+)+");

    EXPECT_FALSE(matcher.is_valid());
}

TEST_F(CachedPatternMatcherTest, MatchGroups) {
    CachedPatternMatcher matcher("sensors/(\\w+)/(\\d+)");

    auto groups = matcher.match_groups("sensors/temp/123");
    ASSERT_TRUE(groups.has_value());
    ASSERT_GE(groups->size(), 2u);
    // First element is full match, subsequent are captured groups
    EXPECT_EQ((*groups)[1], "temp");
    EXPECT_EQ((*groups)[2], "123");
}

TEST_F(CachedPatternMatcherTest, Pattern) {
    CachedPatternMatcher matcher("test.*");
    EXPECT_EQ(matcher.pattern(), "test.*");
}

// ============================================================================
// Integration Tests
// ============================================================================

class PatternMatchingIntegrationTest : public ::testing::Test {};

TEST_F(PatternMatchingIntegrationTest, IndustrialAddressPatterns) {
    FastPatternMatcher matcher;

    // Common industrial patterns
    matcher.add_pattern("ns=2;s=MyServer/MyNode", 1);  // OPC UA exact
    matcher.add_pattern("ns=2;*", 2);                   // OPC UA prefix
    matcher.add_pattern("MB:1:*", 3);                   // Modbus prefix
    matcher.add_pattern("sensors/[a-z]+/temp\\d+", 4);  // Regex pattern

    // Test OPC UA exact
    auto matches = matcher.find_all_matches("ns=2;s=MyServer/MyNode");
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 1u) != matches.end());
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 2u) != matches.end());

    // Test Modbus prefix
    matches = matcher.find_all_matches("MB:1:40001");
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 3u) != matches.end());

    // Test regex
    matches = matcher.find_all_matches("sensors/area/temp123");
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), 4u) != matches.end());
}

TEST_F(PatternMatchingIntegrationTest, HighVolumeRouting) {
    FastPatternMatcher matcher;

    // Simulate large routing table
    for (int i = 0; i < 100; ++i) {
        std::string prefix = "area" + std::to_string(i) + "/*";
        matcher.add_pattern(prefix, static_cast<uint32_t>(i));
    }

    // Performance test
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10000; ++i) {
        auto matches = matcher.find_all_matches("area50/device123/sensor1");
        EXPECT_FALSE(matches.empty());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete in reasonable time
    EXPECT_LT(duration.count(), 1000);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
