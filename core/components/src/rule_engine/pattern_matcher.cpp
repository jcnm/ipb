#include "ipb/core/rule_engine/pattern_matcher.hpp"
#include "ipb/core/rule_engine/compiled_pattern_cache.hpp"
#include <algorithm>
#include <regex>
#include <unordered_map>
#include <optional>

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
// RegexMatcher Implementation - Uses CompiledPatternCache for ReDoS protection
// ============================================================================

class RegexMatcher::Impl {
public:
    explicit Impl(const std::string& pattern)
        : pattern_(pattern)
        , cached_regex_(nullptr)
        , valid_(false) {

        // Use global pattern cache for ReDoS protection and efficiency
        auto result = CompiledPatternCache::global_instance().get_or_compile(pattern);
        if (result) {
            cached_regex_ = result.value();
            valid_ = true;
        } else {
            error_ = result.message();
            valid_ = false;
        }
    }

    bool is_valid() const noexcept { return valid_; }
    const std::string& error() const noexcept { return error_; }

    bool matches(std::string_view input) const noexcept {
        if (!valid_ || !cached_regex_) {
            return false;
        }
        try {
            return std::regex_match(input.begin(), input.end(), *cached_regex_);
        } catch (...) {
            return false;
        }
    }

    PatternMatchResult match_with_groups(std::string_view input) const {
        PatternMatchResult result;
        if (!valid_ || !cached_regex_) {
            result.matched = false;
            return result;
        }
        try {
            std::match_results<std::string_view::const_iterator> match;
            result.matched = std::regex_match(input.begin(), input.end(), match, *cached_regex_);

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
    std::string pattern_;
    const std::regex* cached_regex_;  // Owned by CompiledPatternCache
    bool valid_;
    std::string error_;
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
    // Use pattern validator for ReDoS protection
    auto validation = PatternValidator::validate(pattern);
    if (!validation.is_safe) {
        return false;
    }

    // Also verify syntax
    try {
        std::regex test(pattern.begin(), pattern.end());
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// FastPatternMatcher Implementation - Enterprise-grade composite matcher
// ============================================================================

class FastPatternMatcher::Impl {
public:
    Impl() = default;

    bool add_pattern(std::string_view pattern, uint32_t rule_id,
                    PatternType type) {
        if (type == PatternType::AUTO) {
            type = FastPatternMatcher::detect_type(pattern);
        }

        std::string pattern_str(pattern);

        switch (type) {
            case PatternType::EXACT:
                trie_.add_exact(pattern, rule_id);
                exact_count_++;
                pattern_types_[pattern_str] = type;
                return true;

            case PatternType::PREFIX: {
                // Remove trailing * or / for prefix matching
                std::string_view prefix = pattern;
                if (!prefix.empty() && prefix.back() == '*') {
                    prefix = prefix.substr(0, prefix.size() - 1);
                }
                trie_.add_prefix(prefix, rule_id);
                prefix_count_++;
                pattern_types_[pattern_str] = type;
                return true;
            }

            case PatternType::WILDCARD: {
                wildcard_patterns_.emplace_back(
                    std::make_unique<WildcardMatcher>(pattern_str),
                    rule_id
                );
                wildcard_count_++;
                pattern_types_[pattern_str] = type;
                return true;
            }

            case PatternType::REGEX: {
                // Validate and pre-compile via cache
                auto result = CompiledPatternCache::global_instance()
                    .get_or_compile(pattern);
                if (!result) {
                    return false;  // Invalid pattern
                }
                regex_patterns_.emplace_back(pattern_str, rule_id);
                regex_count_++;
                pattern_types_[pattern_str] = type;
                return true;
            }

            default:
                return false;
        }
    }

    bool remove_pattern(std::string_view pattern) {
        std::string pattern_str(pattern);
        auto it = pattern_types_.find(pattern_str);
        if (it == pattern_types_.end()) {
            return false;
        }

        PatternType type = it->second;
        pattern_types_.erase(it);

        switch (type) {
            case PatternType::EXACT:
                trie_.remove(pattern);
                exact_count_--;
                return true;

            case PatternType::PREFIX: {
                std::string_view prefix = pattern;
                if (!prefix.empty() && prefix.back() == '*') {
                    prefix = prefix.substr(0, prefix.size() - 1);
                }
                trie_.remove(prefix);
                prefix_count_--;
                return true;
            }

            case PatternType::WILDCARD: {
                auto wit = std::find_if(wildcard_patterns_.begin(), wildcard_patterns_.end(),
                    [&pattern_str](const auto& p) { return p.first->pattern() == pattern_str; });
                if (wit != wildcard_patterns_.end()) {
                    wildcard_patterns_.erase(wit);
                    wildcard_count_--;
                }
                return true;
            }

            case PatternType::REGEX: {
                auto rit = std::find_if(regex_patterns_.begin(), regex_patterns_.end(),
                    [&pattern_str](const auto& p) { return p.first == pattern_str; });
                if (rit != regex_patterns_.end()) {
                    regex_patterns_.erase(rit);
                    regex_count_--;
                }
                return true;
            }

            default:
                return false;
        }
    }

    std::vector<uint32_t> find_all_matches(std::string_view input) const {
        std::vector<uint32_t> results;

        // 1. Fast Trie lookup for exact/prefix (O(m))
        auto trie_matches = trie_.find_matches(input);
        results.insert(results.end(), trie_matches.begin(), trie_matches.end());

        // 2. Wildcard patterns (O(w) where w = wildcard pattern count)
        for (const auto& [matcher, rule_id] : wildcard_patterns_) {
            if (matcher->matches(input)) {
                results.push_back(rule_id);
            }
        }

        // 3. Regex patterns (O(r) where r = regex pattern count)
        for (const auto& [pattern, rule_id] : regex_patterns_) {
            CachedPatternMatcher matcher(pattern);
            if (matcher.is_valid() && matcher.matches(input)) {
                results.push_back(rule_id);
            }
        }

        return results;
    }

    bool has_match(std::string_view input) const noexcept {
        // Fast path: check trie first
        if (trie_.matches(input)) {
            return true;
        }

        // Check wildcards
        for (const auto& [matcher, rule_id] : wildcard_patterns_) {
            if (matcher->matches(input)) {
                return true;
            }
        }

        // Check regex
        for (const auto& [pattern, rule_id] : regex_patterns_) {
            CachedPatternMatcher matcher(pattern);
            if (matcher.is_valid() && matcher.matches(input)) {
                return true;
            }
        }

        return false;
    }

    void clear() {
        trie_.clear();
        wildcard_patterns_.clear();
        regex_patterns_.clear();
        pattern_types_.clear();
        exact_count_ = 0;
        prefix_count_ = 0;
        wildcard_count_ = 0;
        regex_count_ = 0;
    }

    FastPatternMatcher::Stats stats() const noexcept {
        FastPatternMatcher::Stats s;
        s.exact_patterns = exact_count_;
        s.prefix_patterns = prefix_count_;
        s.wildcard_patterns = wildcard_count_;
        s.regex_patterns = regex_count_;

        auto trie_stats = trie_.stats();
        s.trie_nodes = trie_stats.node_count;
        s.memory_bytes = trie_stats.memory_bytes;

        // Add wildcard and regex memory
        s.memory_bytes += wildcard_patterns_.size() * 64;
        s.memory_bytes += regex_patterns_.size() * 32;

        return s;
    }

private:
    TrieMatcher trie_;
    std::vector<std::pair<std::unique_ptr<WildcardMatcher>, uint32_t>> wildcard_patterns_;
    std::vector<std::pair<std::string, uint32_t>> regex_patterns_;
    std::unordered_map<std::string, PatternType> pattern_types_;

    size_t exact_count_ = 0;
    size_t prefix_count_ = 0;
    size_t wildcard_count_ = 0;
    size_t regex_count_ = 0;
};

FastPatternMatcher::FastPatternMatcher() : impl_(std::make_unique<Impl>()) {}
FastPatternMatcher::~FastPatternMatcher() = default;
FastPatternMatcher::FastPatternMatcher(FastPatternMatcher&&) noexcept = default;
FastPatternMatcher& FastPatternMatcher::operator=(FastPatternMatcher&&) noexcept = default;

bool FastPatternMatcher::add_pattern(std::string_view pattern, uint32_t rule_id,
                                     PatternType type) {
    return impl_->add_pattern(pattern, rule_id, type);
}

bool FastPatternMatcher::remove_pattern(std::string_view pattern) {
    return impl_->remove_pattern(pattern);
}

std::vector<uint32_t> FastPatternMatcher::find_all_matches(std::string_view input) const {
    return impl_->find_all_matches(input);
}

bool FastPatternMatcher::has_match(std::string_view input) const noexcept {
    return impl_->has_match(input);
}

void FastPatternMatcher::clear() {
    impl_->clear();
}

FastPatternMatcher::Stats FastPatternMatcher::stats() const noexcept {
    return impl_->stats();
}

FastPatternMatcher::PatternType FastPatternMatcher::detect_type(std::string_view pattern) noexcept {
    if (pattern.empty()) {
        return PatternType::EXACT;
    }

    bool has_star = false;
    bool has_question = false;
    bool has_regex_chars = false;
    bool star_at_end_only = false;

    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        switch (c) {
            case '*':
                has_star = true;
                star_at_end_only = (i == pattern.size() - 1);
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
        return PatternType::EXACT;
    }

    // Simple prefix pattern: "prefix*" with no other special chars
    if (has_star && star_at_end_only && !has_question && !has_regex_chars) {
        return PatternType::PREFIX;
    }

    // Has regex characters - full regex
    if (has_regex_chars) {
        return PatternType::REGEX;
    }

    // Wildcards only
    return PatternType::WILDCARD;
}

// ============================================================================
// TrieMatcher Implementation - O(m) lookup for static/prefix patterns
// ============================================================================

/**
 * @brief Trie node for efficient prefix/exact matching
 *
 * Uses a hash map for children to handle arbitrary character sets
 * while maintaining O(1) child lookup.
 */
struct TrieNode {
    std::unordered_map<char, std::unique_ptr<TrieNode>> children;
    std::optional<uint32_t> exact_rule_id;      // Rule ID if this is end of exact pattern
    std::vector<uint32_t> prefix_rule_ids;       // Rule IDs for prefix patterns ending here

    bool is_end_of_pattern() const noexcept {
        return exact_rule_id.has_value() || !prefix_rule_ids.empty();
    }
};

class TrieMatcher::Impl {
public:
    Impl() : root_(std::make_unique<TrieNode>()), pattern_count_(0), node_count_(1) {}

    void add_exact(std::string_view pattern, uint32_t rule_id) {
        TrieNode* node = root_.get();

        for (char c : pattern) {
            auto& child = node->children[c];
            if (!child) {
                child = std::make_unique<TrieNode>();
                ++node_count_;
            }
            node = child.get();
        }

        node->exact_rule_id = rule_id;
        ++pattern_count_;
    }

    void add_prefix(std::string_view prefix, uint32_t rule_id) {
        TrieNode* node = root_.get();

        for (char c : prefix) {
            auto& child = node->children[c];
            if (!child) {
                child = std::make_unique<TrieNode>();
                ++node_count_;
            }
            node = child.get();
        }

        node->prefix_rule_ids.push_back(rule_id);
        ++pattern_count_;
    }

    std::vector<uint32_t> find_matches(std::string_view input) const noexcept {
        std::vector<uint32_t> results;
        const TrieNode* node = root_.get();

        // Collect prefix matches as we traverse
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];

            auto it = node->children.find(c);
            if (it == node->children.end()) {
                break;  // No more matches possible
            }

            node = it->second.get();

            // Add any prefix rule IDs at this position
            for (uint32_t id : node->prefix_rule_ids) {
                results.push_back(id);
            }
        }

        // Check for exact match at the end
        if (node && node->exact_rule_id.has_value()) {
            // Insert exact match at beginning (higher priority)
            results.insert(results.begin(), *node->exact_rule_id);
        }

        return results;
    }

    std::optional<uint32_t> find_exact(std::string_view input) const noexcept {
        const TrieNode* node = root_.get();

        for (char c : input) {
            auto it = node->children.find(c);
            if (it == node->children.end()) {
                return std::nullopt;
            }
            node = it->second.get();
        }

        return node->exact_rule_id;
    }

    bool matches(std::string_view input) const noexcept {
        const TrieNode* node = root_.get();

        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];

            auto it = node->children.find(c);
            if (it == node->children.end()) {
                return false;
            }

            node = it->second.get();

            // Check for prefix match
            if (!node->prefix_rule_ids.empty()) {
                return true;
            }
        }

        // Check for exact match
        return node && node->exact_rule_id.has_value();
    }

    bool remove(std::string_view pattern) {
        // Find the node for this pattern
        std::vector<std::pair<TrieNode*, char>> path;
        TrieNode* node = root_.get();

        for (char c : pattern) {
            auto it = node->children.find(c);
            if (it == node->children.end()) {
                return false;  // Pattern not found
            }
            path.push_back({node, c});
            node = it->second.get();
        }

        // Remove the pattern markers
        bool removed = false;
        if (node->exact_rule_id.has_value()) {
            node->exact_rule_id.reset();
            removed = true;
            --pattern_count_;
        }

        if (!node->prefix_rule_ids.empty()) {
            pattern_count_ -= node->prefix_rule_ids.size();
            node->prefix_rule_ids.clear();
            removed = true;
        }

        // Clean up empty nodes (bottom-up)
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            TrieNode* parent = it->first;
            char c = it->second;

            auto child_it = parent->children.find(c);
            if (child_it != parent->children.end()) {
                TrieNode* child = child_it->second.get();
                if (child->children.empty() && !child->is_end_of_pattern()) {
                    parent->children.erase(child_it);
                    --node_count_;
                } else {
                    break;  // Can't remove, has children or is pattern end
                }
            }
        }

        return removed;
    }

    void clear() {
        root_ = std::make_unique<TrieNode>();
        pattern_count_ = 0;
        node_count_ = 1;
    }

    size_t size() const noexcept { return pattern_count_; }
    bool empty() const noexcept { return pattern_count_ == 0; }

    TrieMatcher::Stats stats() const noexcept {
        TrieMatcher::Stats s;
        s.pattern_count = pattern_count_;
        s.node_count = node_count_;
        // Approximate memory: each node has map overhead + pointers
        s.memory_bytes = node_count_ * (sizeof(TrieNode) + 64);  // 64 for map overhead
        return s;
    }

private:
    std::unique_ptr<TrieNode> root_;
    size_t pattern_count_;
    size_t node_count_;
};

TrieMatcher::TrieMatcher() : impl_(std::make_unique<Impl>()) {}
TrieMatcher::~TrieMatcher() = default;
TrieMatcher::TrieMatcher(TrieMatcher&&) noexcept = default;
TrieMatcher& TrieMatcher::operator=(TrieMatcher&&) noexcept = default;

void TrieMatcher::add_exact(std::string_view pattern, uint32_t rule_id) {
    impl_->add_exact(pattern, rule_id);
}

void TrieMatcher::add_prefix(std::string_view prefix, uint32_t rule_id) {
    impl_->add_prefix(prefix, rule_id);
}

std::vector<uint32_t> TrieMatcher::find_matches(std::string_view input) const noexcept {
    return impl_->find_matches(input);
}

std::optional<uint32_t> TrieMatcher::find_exact(std::string_view input) const noexcept {
    return impl_->find_exact(input);
}

bool TrieMatcher::matches(std::string_view input) const noexcept {
    return impl_->matches(input);
}

bool TrieMatcher::remove(std::string_view pattern) {
    return impl_->remove(pattern);
}

void TrieMatcher::clear() {
    impl_->clear();
}

size_t TrieMatcher::size() const noexcept {
    return impl_->size();
}

bool TrieMatcher::empty() const noexcept {
    return impl_->empty();
}

TrieMatcher::Stats TrieMatcher::stats() const noexcept {
    return impl_->stats();
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
