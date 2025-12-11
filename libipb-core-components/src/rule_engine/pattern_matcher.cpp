#include "ipb/core/rule_engine/pattern_matcher.hpp"
#include <regex>
#include <algorithm>

namespace ipb::core {

// ============================================================================
// ExactMatcher Implementation
// ============================================================================

ExactMatcher::ExactMatcher(std::string pattern)
    : pattern_(std::move(pattern)) {}

bool ExactMatcher::matches(std::string_view input) const noexcept {
    return input == pattern_;
}

PatternMatchResult ExactMatcher::match_with_groups(std::string_view input) const {
    PatternMatchResult result;
    result.matched = matches(input);
    return result;
}

// ============================================================================
// PrefixMatcher Implementation
// ============================================================================

PrefixMatcher::PrefixMatcher(std::string prefix)
    : prefix_(std::move(prefix)) {}

bool PrefixMatcher::matches(std::string_view input) const noexcept {
    if (input.size() < prefix_.size()) {
        return false;
    }
    return input.substr(0, prefix_.size()) == prefix_;
}

PatternMatchResult PrefixMatcher::match_with_groups(std::string_view input) const {
    PatternMatchResult result;
    result.matched = matches(input);
    if (result.matched && input.size() > prefix_.size()) {
        result.captured_groups.emplace_back(input.substr(prefix_.size()));
    }
    return result;
}

// ============================================================================
// WildcardMatcher Implementation
// ============================================================================

WildcardMatcher::WildcardMatcher(std::string pattern)
    : pattern_(std::move(pattern)) {}

bool WildcardMatcher::matches(std::string_view input) const noexcept {
    return match_impl(pattern_.c_str(), input.data());
}

bool WildcardMatcher::match_impl(const char* pattern, const char* input) const noexcept {
    // Optimized wildcard matching using two-pointer technique
    const char* star = nullptr;
    const char* ss = nullptr;

    while (*input) {
        // Match single character or ?
        if (*pattern == *input || *pattern == '?') {
            ++pattern;
            ++input;
            continue;
        }

        // Star found - save position
        if (*pattern == '*') {
            star = pattern++;
            ss = input;
            continue;
        }

        // Mismatch but we have a star - backtrack
        if (star) {
            pattern = star + 1;
            input = ++ss;
            continue;
        }

        return false;
    }

    // Skip trailing stars
    while (*pattern == '*') {
        ++pattern;
    }

    return *pattern == '\0';
}

PatternMatchResult WildcardMatcher::match_with_groups(std::string_view input) const {
    PatternMatchResult result;
    result.matched = matches(input);
    // Wildcard matcher doesn't capture groups
    return result;
}

// ============================================================================
// RegexMatcher Implementation
// ============================================================================

class RegexMatcher::Impl {
public:
    explicit Impl(const std::string& pattern)
        : regex_(pattern, std::regex::ECMAScript | std::regex::optimize) {}

    bool matches(std::string_view input) const noexcept {
        try {
            return std::regex_match(input.begin(), input.end(), regex_);
        } catch (...) {
            return false;
        }
    }

    PatternMatchResult match_with_groups(std::string_view input) const {
        PatternMatchResult result;
        try {
            std::match_results<std::string_view::const_iterator> match;
            result.matched = std::regex_match(input.begin(), input.end(), match, regex_);

            if (result.matched) {
                // Skip first group (entire match)
                for (size_t i = 1; i < match.size(); ++i) {
                    result.captured_groups.emplace_back(match[i].str());
                }
            }
        } catch (...) {
            result.matched = false;
        }
        return result;
    }

private:
    std::regex regex_;
};

RegexMatcher::RegexMatcher(std::string pattern)
    : pattern_(std::move(pattern)) {
    impl_ = std::make_unique<Impl>(pattern_);
}

RegexMatcher::~RegexMatcher() = default;

bool RegexMatcher::matches(std::string_view input) const noexcept {
    return impl_->matches(input);
}

PatternMatchResult RegexMatcher::match_with_groups(std::string_view input) const {
    return impl_->match_with_groups(input);
}

bool RegexMatcher::is_valid_regex(std::string_view pattern) noexcept {
    try {
        std::regex test(pattern.begin(), pattern.end());
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// CTREMatcher Implementation (when available)
// ============================================================================

#ifdef IPB_HAS_CTRE
CTREMatcher::CTREMatcher(std::string pattern)
    : pattern_(std::move(pattern)) {
    // For runtime patterns, we fall back to std::regex
    // True CTRE patterns must be known at compile time
    fallback_ = std::make_unique<RegexMatcher>(pattern_);
}

bool CTREMatcher::matches(std::string_view input) const noexcept {
    // Check against pre-compiled patterns first for common formats
    // Then fall back to runtime regex

    // Fast path: try common industrial patterns
    if (pattern_.starts_with("ns=")) {
        auto m = patterns::match_opcua(input);
        if (m) return true;
    } else if (pattern_.starts_with("MB:")) {
        auto m = patterns::match_modbus(input);
        if (m) return true;
    } else if (pattern_.starts_with("spBv1.0")) {
        auto m = patterns::match_sparkplug(input);
        if (m) return true;
    } else if (pattern_.starts_with("sensors/")) {
        auto m = patterns::match_sensor(input);
        if (m) return true;
    } else if (pattern_.starts_with("alarms/")) {
        auto m = patterns::match_alarm(input);
        if (m) return true;
    }

    // Fallback to runtime regex
    return fallback_->matches(input);
}

PatternMatchResult CTREMatcher::match_with_groups(std::string_view input) const {
    return fallback_->match_with_groups(input);
}
#endif // IPB_HAS_CTRE

// ============================================================================
// PatternMatcherFactory Implementation
// ============================================================================

std::unique_ptr<IPatternMatcher> PatternMatcherFactory::create(
        std::string_view pattern,
        MatcherType type) {

    if (type == MatcherType::AUTO) {
        type = analyze_pattern(pattern);
    }

    std::string pattern_str(pattern);

    switch (type) {
        case MatcherType::EXACT:
            return std::make_unique<ExactMatcher>(std::move(pattern_str));

        case MatcherType::PREFIX:
            return std::make_unique<PrefixMatcher>(std::move(pattern_str));

        case MatcherType::WILDCARD:
            return std::make_unique<WildcardMatcher>(std::move(pattern_str));

        case MatcherType::REGEX_CTRE:
#ifdef IPB_HAS_CTRE
            return std::make_unique<CTREMatcher>(std::move(pattern_str));
#endif
            // Fall through to REGEX_RUNTIME if CTRE not available
            [[fallthrough]];

        case MatcherType::REGEX_RUNTIME:
        case MatcherType::SUFFIX:
        case MatcherType::AUTO:
        default:
            return std::make_unique<RegexMatcher>(std::move(pattern_str));
    }
}

PatternMatcherFactory::MatcherType
PatternMatcherFactory::analyze_pattern(std::string_view pattern) noexcept {
    if (pattern.empty()) {
        return MatcherType::EXACT;
    }

    bool has_star = false;
    bool has_question = false;
    bool has_regex_chars = false;

    for (char c : pattern) {
        switch (c) {
            case '*':
                has_star = true;
                break;
            case '?':
                has_question = true;
                break;
            case '.':
            case '+':
            case '^':
            case '$':
            case '[':
            case ']':
            case '(':
            case ')':
            case '{':
            case '}':
            case '|':
            case '\\':
                has_regex_chars = true;
                break;
        }
    }

    // No special characters - exact match
    if (!has_star && !has_question && !has_regex_chars) {
        return MatcherType::EXACT;
    }

    // Simple wildcard only (no regex metacharacters)
    if ((has_star || has_question) && !has_regex_chars) {
        // Check for simple prefix pattern: "prefix*"
        if (has_star && !has_question &&
            pattern.find('*') == pattern.size() - 1) {
            return MatcherType::PREFIX;
        }
        return MatcherType::WILDCARD;
    }

    // Has regex characters - use full regex
#ifdef IPB_HAS_CTRE
    return MatcherType::REGEX_CTRE;
#else
    return MatcherType::REGEX_RUNTIME;
#endif
}

} // namespace ipb::core
