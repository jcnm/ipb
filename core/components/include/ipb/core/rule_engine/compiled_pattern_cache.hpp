#pragma once

/**
 * @file compiled_pattern_cache.hpp
 * @brief Thread-safe cache for compiled regex patterns with ReDoS protection
 *
 * This component addresses the critical ReDoS vulnerability by:
 * 1. Pre-compiling regex patterns at rule creation time (not per-message)
 * 2. Caching compiled patterns for O(1) lookup
 * 3. Validating patterns to reject dangerous constructs
 * 4. Providing timeout protection for pattern compilation
 *
 * Enterprise-grade features:
 * - Thread-safe with shared_mutex for concurrent reads
 * - LRU eviction when cache is full
 * - Pattern complexity analysis
 * - Compilation timeout protection
 * - Metrics for monitoring
 */

#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ipb::core {

/**
 * @brief Result of pattern validation
 */
struct PatternValidationResult {
    bool is_safe = false;
    std::string reason;
    size_t estimated_complexity = 0;
    bool has_backreferences     = false;
    bool has_nested_quantifiers = false;
    size_t max_repetition_depth = 0;

    explicit operator bool() const noexcept { return is_safe; }
};

/**
 * @brief Statistics for pattern cache monitoring
 */
struct PatternCacheStats {
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> compilations{0};
    std::atomic<uint64_t> compilation_failures{0};
    std::atomic<uint64_t> validation_rejections{0};
    std::atomic<uint64_t> timeout_rejections{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<int64_t> total_compilation_time_ns{0};

    double hit_rate() const noexcept {
        auto total = cache_hits.load() + cache_misses.load();
        return total > 0 ? static_cast<double>(cache_hits.load()) / total * 100.0 : 0.0;
    }

    double avg_compilation_time_us() const noexcept {
        auto count = compilations.load();
        return count > 0 ? static_cast<double>(total_compilation_time_ns.load()) / count / 1000.0
                         : 0.0;
    }

    void reset() noexcept {
        cache_hits.store(0);
        cache_misses.store(0);
        compilations.store(0);
        compilation_failures.store(0);
        validation_rejections.store(0);
        timeout_rejections.store(0);
        evictions.store(0);
        total_compilation_time_ns.store(0);
    }
};

/**
 * @brief Configuration for pattern cache
 */
struct PatternCacheConfig {
    /// Maximum number of cached patterns
    size_t max_size = 10000;

    /// Maximum pattern length (bytes)
    size_t max_pattern_length = 1024;

    /// Compilation timeout (0 = no timeout)
    std::chrono::milliseconds compilation_timeout{100};

    /// Enable dangerous pattern validation
    bool enable_validation = true;

    /// Maximum allowed pattern complexity score
    size_t max_complexity = 50;

    /// Regex flags to use
    std::regex_constants::syntax_option_type regex_flags =
        std::regex_constants::ECMAScript | std::regex_constants::optimize;
};

/**
 * @brief Cached compiled pattern entry
 */
struct CachedPattern {
    std::string pattern_string;
    std::unique_ptr<std::regex> compiled;
    std::chrono::steady_clock::time_point compiled_at;
    std::chrono::nanoseconds compilation_time{0};
    size_t complexity_score{0};
    mutable std::atomic<uint64_t> use_count{0};

    CachedPattern() = default;

    // Custom move constructor (atomic requires special handling)
    CachedPattern(CachedPattern&& other) noexcept
        : pattern_string(std::move(other.pattern_string)), compiled(std::move(other.compiled)),
          compiled_at(other.compiled_at), compilation_time(other.compilation_time),
          complexity_score(other.complexity_score),
          use_count(other.use_count.load(std::memory_order_relaxed)) {}

    // Custom move assignment
    CachedPattern& operator=(CachedPattern&& other) noexcept {
        if (this != &other) {
            pattern_string   = std::move(other.pattern_string);
            compiled         = std::move(other.compiled);
            compiled_at      = other.compiled_at;
            compilation_time = other.compilation_time;
            complexity_score = other.complexity_score;
            use_count.store(other.use_count.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
        }
        return *this;
    }

    // Non-copyable due to unique_ptr and atomic
    CachedPattern(const CachedPattern&)            = delete;
    CachedPattern& operator=(const CachedPattern&) = delete;
};

/**
 * @brief Pattern validator for ReDoS protection
 *
 * Analyzes patterns to detect potentially dangerous constructs:
 * - Nested quantifiers: (a+)+, (a*)*
 * - Overlapping alternations: (a|a)+
 * - Backreferences with quantifiers
 * - Excessive repetition depth
 */
class PatternValidator {
public:
    /**
     * @brief Validate a pattern for safety
     * @param pattern The regex pattern to validate
     * @param max_complexity Maximum allowed complexity score
     * @return Validation result with details
     */
    static PatternValidationResult validate(std::string_view pattern,
                                            size_t max_complexity = 50) noexcept;

    /**
     * @brief Check if pattern contains nested quantifiers
     * @param pattern The regex pattern to check
     * @return true if dangerous nested quantifiers found
     */
    static bool has_nested_quantifiers(std::string_view pattern) noexcept;

    /**
     * @brief Calculate pattern complexity score
     * @param pattern The regex pattern to analyze
     * @return Complexity score (higher = more dangerous)
     *
     * Scoring:
     * - Each quantifier (+, *, ?, {n,m}): +1
     * - Each group: +2
     * - Nested quantifiers: +10
     * - Backreferences: +5
     * - Alternation in quantified group: +5
     */
    static size_t calculate_complexity(std::string_view pattern) noexcept;

private:
    // Dangerous pattern signatures (pre-compiled for efficiency)
    static bool check_nested_plus(std::string_view pattern) noexcept;
    static bool check_nested_star(std::string_view pattern) noexcept;
    static bool check_alternation_in_quantified_group(std::string_view pattern) noexcept;
    static size_t count_quantifiers(std::string_view pattern) noexcept;
    static size_t count_groups(std::string_view pattern) noexcept;
};

/**
 * @brief Thread-safe LRU cache for compiled regex patterns
 *
 * This cache eliminates the ReDoS vulnerability by:
 * 1. Compiling patterns once at rule creation, not per-message
 * 2. Validating patterns before compilation
 * 3. Providing O(1) lookup for compiled patterns
 *
 * Thread Safety:
 * - Multiple threads can read concurrently (shared_mutex)
 * - Writes are serialized
 * - Pattern matching is lock-free after initial lookup
 *
 * Example usage:
 * @code
 * CompiledPatternCache cache;
 *
 * // Compile pattern (done once at rule creation)
 * auto result = cache.get_or_compile("sensors/temp.*");
 * if (!result) {
 *     // Handle compilation error
 * }
 *
 * // Match against compiled pattern (fast, no compilation)
 * const std::regex* pattern = cache.get("sensors/temp.*");
 * if (pattern) {
 *     bool matches = std::regex_match(address, *pattern);
 * }
 * @endcode
 */
class CompiledPatternCache {
public:
    CompiledPatternCache();
    explicit CompiledPatternCache(const PatternCacheConfig& config);
    ~CompiledPatternCache();

    // Non-copyable, movable
    CompiledPatternCache(const CompiledPatternCache&)            = delete;
    CompiledPatternCache& operator=(const CompiledPatternCache&) = delete;
    CompiledPatternCache(CompiledPatternCache&&) noexcept;
    CompiledPatternCache& operator=(CompiledPatternCache&&) noexcept;

    /**
     * @brief Get compiled pattern from cache
     * @param pattern The pattern string
     * @return Pointer to compiled regex or nullptr if not cached
     *
     * Thread-safe: Uses shared lock for concurrent reads
     */
    const std::regex* get(std::string_view pattern) const noexcept;

    /**
     * @brief Get or compile pattern
     * @param pattern The pattern string
     * @return Success with compiled regex pointer, or error
     *
     * Thread-safe: Uses exclusive lock for compilation
     *
     * Error codes:
     * - INVALID_PATTERN: Pattern validation failed (ReDoS risk)
     * - PATTERN_COMPILE_TIMEOUT: Compilation exceeded timeout
     * - INVALID_ARGUMENT: Pattern too long or empty
     */
    common::Result<const std::regex*> get_or_compile(std::string_view pattern);

    /**
     * @brief Pre-compile a pattern without returning it
     * @param pattern The pattern string
     * @return Success or error
     *
     * Use this at rule creation time to fail fast on invalid patterns.
     */
    common::Result<void> precompile(std::string_view pattern);

    /**
     * @brief Validate pattern without compiling
     * @param pattern The pattern string
     * @return Validation result
     */
    PatternValidationResult validate(std::string_view pattern) const noexcept;

    /**
     * @brief Remove pattern from cache
     * @param pattern The pattern string
     * @return true if pattern was in cache
     */
    bool remove(std::string_view pattern);

    /**
     * @brief Clear all cached patterns
     */
    void clear();

    /**
     * @brief Get current cache size
     */
    size_t size() const noexcept;

    /**
     * @brief Check if pattern is cached
     */
    bool contains(std::string_view pattern) const noexcept;

    /**
     * @brief Get cache statistics
     */
    const PatternCacheStats& stats() const noexcept { return stats_; }

    /**
     * @brief Reset statistics
     */
    void reset_stats() noexcept { stats_.reset(); }

    /**
     * @brief Get configuration
     */
    const PatternCacheConfig& config() const noexcept { return config_; }

    /**
     * @brief Get singleton instance for global pattern cache
     *
     * Use this for patterns shared across multiple rules/components.
     */
    static CompiledPatternCache& global_instance();

private:
    PatternCacheConfig config_;
    mutable PatternCacheStats stats_;

    // LRU cache implementation
    using LRUList = std::list<std::string>;
    mutable std::unordered_map<std::string, std::pair<CachedPattern, LRUList::iterator>> cache_;
    mutable LRUList lru_list_;
    mutable std::shared_mutex mutex_;

    // Internal helpers
    common::Result<CachedPattern> compile_pattern(std::string_view pattern);
    void evict_lru();
    void touch_lru(const std::string& pattern) const;
};

/**
 * @brief RAII helper for pattern matching with automatic cache lookup
 *
 * Example:
 * @code
 * PatternMatcher matcher("sensors/.*");
 * if (matcher.is_valid()) {
 *     bool matches = matcher.matches("sensors/temp1");
 * }
 * @endcode
 */
class CachedPatternMatcher {
public:
    explicit CachedPatternMatcher(std::string_view pattern);
    CachedPatternMatcher(std::string_view pattern, CompiledPatternCache& cache);

    /**
     * @brief Check if pattern was successfully compiled
     */
    bool is_valid() const noexcept { return compiled_ != nullptr; }

    /**
     * @brief Get compilation error if invalid
     */
    const std::string& error() const noexcept { return error_; }

    /**
     * @brief Match input against pattern
     * @param input The string to match
     * @return true if input matches pattern
     *
     * Note: Returns false if pattern is invalid
     */
    bool matches(std::string_view input) const noexcept;

    /**
     * @brief Match with capture groups
     * @param input The string to match
     * @return Match results with captured groups
     */
    std::optional<std::vector<std::string>> match_groups(std::string_view input) const;

    /**
     * @brief Get the pattern string
     */
    std::string_view pattern() const noexcept { return pattern_; }

private:
    std::string pattern_;
    const std::regex* compiled_ = nullptr;
    std::string error_;
};

}  // namespace ipb::core
