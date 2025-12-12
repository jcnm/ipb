#pragma once

/**
 * @file pattern_matcher.hpp
 * @brief High-performance pattern matching with CTRE support
 *
 * Provides compile-time and runtime pattern matching optimized for
 * industrial address formats.
 */

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Use CTRE if available for compile-time regex
#ifdef IPB_HAS_CTRE
#include <ctre.hpp>
#endif

namespace ipb::core {

/**
 * @brief Result of a pattern match operation
 */
struct PatternMatchResult {
    bool matched = false;
    std::vector<std::string> captured_groups;

    explicit operator bool() const noexcept { return matched; }
};

/**
 * @brief Abstract pattern matcher interface
 */
class IPatternMatcher {
public:
    virtual ~IPatternMatcher() = default;

    /// Check if input matches the pattern
    virtual bool matches(std::string_view input) const noexcept = 0;

    /// Match with capture groups
    virtual PatternMatchResult match_with_groups(std::string_view input) const = 0;

    /// Get the original pattern string
    virtual std::string_view pattern() const noexcept = 0;

    /// Check if this is a compile-time matcher
    virtual bool is_compile_time() const noexcept = 0;
};

/**
 * @brief Factory for creating pattern matchers
 *
 * Automatically selects the best matcher type based on:
 * 1. Pattern complexity
 * 2. CTRE availability
 * 3. Runtime vs compile-time requirements
 */
class PatternMatcherFactory {
public:
    /// Matcher type hints
    enum class MatcherType {
        AUTO,           ///< Automatically select best matcher
        EXACT,          ///< Exact string comparison (fastest)
        PREFIX,         ///< Prefix matching
        SUFFIX,         ///< Suffix matching
        WILDCARD,       ///< Simple wildcard (* and ?)
        REGEX_RUNTIME,  ///< std::regex (flexible but slower)
        REGEX_CTRE      ///< CTRE compile-time regex (when available)
    };

    /// Create a matcher for the given pattern
    static std::unique_ptr<IPatternMatcher> create(
        std::string_view pattern,
        MatcherType type = MatcherType::AUTO);

    /// Check if CTRE is available
    static constexpr bool has_ctre() noexcept {
#ifdef IPB_HAS_CTRE
        return true;
#else
        return false;
#endif
    }

    /// Analyze pattern and suggest best matcher type
    static MatcherType analyze_pattern(std::string_view pattern) noexcept;
};

/**
 * @brief Exact string matcher (O(n) comparison)
 */
class ExactMatcher : public IPatternMatcher {
public:
    explicit ExactMatcher(std::string pattern);

    bool matches(std::string_view input) const noexcept override;
    PatternMatchResult match_with_groups(std::string_view input) const override;
    std::string_view pattern() const noexcept override { return pattern_; }
    bool is_compile_time() const noexcept override { return false; }

private:
    std::string pattern_;
};

/**
 * @brief Prefix matcher (O(m) where m = prefix length)
 */
class PrefixMatcher : public IPatternMatcher {
public:
    explicit PrefixMatcher(std::string prefix);

    bool matches(std::string_view input) const noexcept override;
    PatternMatchResult match_with_groups(std::string_view input) const override;
    std::string_view pattern() const noexcept override { return prefix_; }
    bool is_compile_time() const noexcept override { return false; }

private:
    std::string prefix_;
};

/**
 * @brief Simple wildcard matcher (* matches any sequence, ? matches single char)
 *
 * More deterministic than full regex, suitable for industrial use.
 */
class WildcardMatcher : public IPatternMatcher {
public:
    explicit WildcardMatcher(std::string pattern);

    bool matches(std::string_view input) const noexcept override;
    PatternMatchResult match_with_groups(std::string_view input) const override;
    std::string_view pattern() const noexcept override { return pattern_; }
    bool is_compile_time() const noexcept override { return false; }

private:
    std::string pattern_;

    /// Optimized wildcard matching algorithm
    bool match_impl(const char* pattern, const char* input) const noexcept;
};

/**
 * @brief Runtime regex matcher using std::regex
 */
class RegexMatcher : public IPatternMatcher {
public:
    explicit RegexMatcher(std::string pattern);
    ~RegexMatcher();

    bool matches(std::string_view input) const noexcept override;
    PatternMatchResult match_with_groups(std::string_view input) const override;
    std::string_view pattern() const noexcept override { return pattern_; }
    bool is_compile_time() const noexcept override { return false; }

    /// Check if pattern is valid regex
    static bool is_valid_regex(std::string_view pattern) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::string pattern_;
};

/**
 * @brief Trie-based matcher for O(m) prefix/exact matching
 *
 * Uses a trie data structure to efficiently match addresses against
 * multiple patterns. Ideal for large routing tables with static or
 * prefix-based rules.
 *
 * Performance:
 * - Lookup: O(m) where m is input string length
 * - Insert: O(m) where m is pattern length
 * - Memory: O(n*avg_len) where n is number of patterns
 *
 * This is much more efficient than checking each pattern individually
 * (O(n*m)) when there are many static routing rules.
 */
class TrieMatcher {
public:
    TrieMatcher();
    ~TrieMatcher();

    // Non-copyable but movable
    TrieMatcher(const TrieMatcher&) = delete;
    TrieMatcher& operator=(const TrieMatcher&) = delete;
    TrieMatcher(TrieMatcher&&) noexcept;
    TrieMatcher& operator=(TrieMatcher&&) noexcept;

    /**
     * @brief Add an exact pattern to match
     * @param pattern The exact string to match
     * @param rule_id Associated rule ID for this pattern
     */
    void add_exact(std::string_view pattern, uint32_t rule_id);

    /**
     * @brief Add a prefix pattern to match
     * @param prefix The prefix to match (input must start with this)
     * @param rule_id Associated rule ID for this pattern
     */
    void add_prefix(std::string_view prefix, uint32_t rule_id);

    /**
     * @brief Find all matching rule IDs for an input string
     * @param input The input string to match
     * @return Vector of matching rule IDs (exact matches first, then prefix)
     *
     * Complexity: O(m) where m is input length
     */
    std::vector<uint32_t> find_matches(std::string_view input) const noexcept;

    /**
     * @brief Check if there's any exact match for input
     * @param input The input string to check
     * @return Optional rule ID if exact match found
     *
     * Complexity: O(m) where m is input length
     */
    std::optional<uint32_t> find_exact(std::string_view input) const noexcept;

    /**
     * @brief Check if any pattern (exact or prefix) matches input
     * @param input The input string to check
     * @return true if any match found
     *
     * Complexity: O(m) where m is input length
     */
    bool matches(std::string_view input) const noexcept;

    /**
     * @brief Remove a pattern from the trie
     * @param pattern The pattern to remove
     * @return true if pattern was found and removed
     */
    bool remove(std::string_view pattern);

    /**
     * @brief Clear all patterns
     */
    void clear();

    /**
     * @brief Get number of patterns stored
     */
    size_t size() const noexcept;

    /**
     * @brief Check if trie is empty
     */
    bool empty() const noexcept;

    /**
     * @brief Get memory usage statistics
     */
    struct Stats {
        size_t pattern_count = 0;
        size_t node_count = 0;
        size_t memory_bytes = 0;
    };
    Stats stats() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#ifdef IPB_HAS_CTRE
/**
 * @brief Compile-time regex matcher using CTRE
 *
 * For patterns known at compile time, CTRE provides:
 * - Zero runtime regex compilation
 * - Deterministic O(n) matching
 * - No dynamic memory allocation during matching
 *
 * Common industrial patterns are pre-compiled as static matchers.
 */
class CTREMatcher : public IPatternMatcher {
public:
    /// Create from runtime pattern (falls back to regex matching)
    explicit CTREMatcher(std::string pattern);

    bool matches(std::string_view input) const noexcept override;
    PatternMatchResult match_with_groups(std::string_view input) const override;
    std::string_view pattern() const noexcept override { return pattern_; }
    bool is_compile_time() const noexcept override { return true; }

private:
    std::string pattern_;
    std::unique_ptr<RegexMatcher> fallback_;
};

// Pre-compiled common industrial patterns
namespace patterns {

/// OPC UA Node ID pattern: ns=N;s=...
inline constexpr auto OPC_UA_NODE_ID = ctll::fixed_string{R"(ns=(\d+);s=(.+))"};

/// Modbus address pattern: MB:UNIT:ADDR
inline constexpr auto MODBUS_ADDRESS = ctll::fixed_string{R"(MB:(\d+):(\d+))"};

/// Sparkplug B topic pattern: spBv1.0/GROUP/MESSAGE_TYPE/EDGE/DEVICE
inline constexpr auto SPARKPLUG_TOPIC =
    ctll::fixed_string{R"(spBv1\.0/([^/]+)/([^/]+)/([^/]+)(?:/([^/]+))?)"};

/// Generic sensor pattern: sensors/TYPE/ID
inline constexpr auto SENSOR_ADDRESS = ctll::fixed_string{R"(sensors/(\w+)/(\w+))"};

/// Alarm pattern: alarms/LEVEL/SOURCE
inline constexpr auto ALARM_ADDRESS = ctll::fixed_string{R"(alarms/(critical|warning|info)/(\w+))"};

/// Check OPC UA node ID
template<typename Input>
constexpr auto match_opcua(Input&& input) noexcept {
    return ctre::match<OPC_UA_NODE_ID>(std::forward<Input>(input));
}

/// Check Modbus address
template<typename Input>
constexpr auto match_modbus(Input&& input) noexcept {
    return ctre::match<MODBUS_ADDRESS>(std::forward<Input>(input));
}

/// Check Sparkplug topic
template<typename Input>
constexpr auto match_sparkplug(Input&& input) noexcept {
    return ctre::match<SPARKPLUG_TOPIC>(std::forward<Input>(input));
}

/// Check sensor address
template<typename Input>
constexpr auto match_sensor(Input&& input) noexcept {
    return ctre::match<SENSOR_ADDRESS>(std::forward<Input>(input));
}

/// Check alarm address
template<typename Input>
constexpr auto match_alarm(Input&& input) noexcept {
    return ctre::match<ALARM_ADDRESS>(std::forward<Input>(input));
}

} // namespace patterns
#endif // IPB_HAS_CTRE

} // namespace ipb::core
