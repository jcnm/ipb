#include <ipb/common/debug.hpp>
#include <ipb/core/rule_engine/compiled_pattern_cache.hpp>

#include <algorithm>
#include <future>
#include <thread>

namespace ipb::core {

using namespace common;
using namespace common::debug;

namespace {
constexpr const char* LOG_CAT = "PatternCache";
}  // namespace

// ============================================================================
// PatternValidator Implementation
// ============================================================================

PatternValidationResult PatternValidator::validate(std::string_view pattern,
                                                   size_t max_complexity) noexcept {
    PatternValidationResult result;

    // Empty pattern check
    if (pattern.empty()) {
        result.is_safe = false;
        result.reason  = "Pattern cannot be empty";
        return result;
    }

    // Calculate complexity
    result.estimated_complexity = calculate_complexity(pattern);

    // Check for nested quantifiers (main ReDoS vector)
    result.has_nested_quantifiers = has_nested_quantifiers(pattern);

    // Check for backreferences
    result.has_backreferences = (pattern.find("\\1") != std::string_view::npos ||
                                 pattern.find("\\2") != std::string_view::npos ||
                                 pattern.find("\\3") != std::string_view::npos);

    // Determine safety
    if (result.has_nested_quantifiers) {
        result.is_safe = false;
        result.reason  = "Pattern contains nested quantifiers - potential ReDoS";
        return result;
    }

    if (result.estimated_complexity > max_complexity) {
        result.is_safe = false;
        result.reason  = "Pattern complexity (" + std::to_string(result.estimated_complexity) +
                        ") exceeds maximum (" + std::to_string(max_complexity) + ")";
        return result;
    }

    result.is_safe = true;
    result.reason  = "OK";
    return result;
}

bool PatternValidator::has_nested_quantifiers(std::string_view pattern) noexcept {
    return check_nested_plus(pattern) || check_nested_star(pattern) ||
           check_alternation_in_quantified_group(pattern);
}

bool PatternValidator::check_nested_plus(std::string_view pattern) noexcept {
    // Detect patterns like (a+)+, (.+)+, ([^/]+)+
    size_t depth             = 0;
    bool in_quantified_group = false;

    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];

        // Skip escaped characters
        if (c == '\\' && i + 1 < pattern.size()) {
            ++i;
            continue;
        }

        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (depth > 0) {
                --depth;
                // Check if closing paren is followed by quantifier
                if (i + 1 < pattern.size()) {
                    char next = pattern[i + 1];
                    if (next == '+' || next == '*' || next == '{') {
                        in_quantified_group = true;
                    }
                }
            }
        } else if ((c == '+' || c == '*') && depth > 0 && in_quantified_group) {
            // Found quantifier inside a quantified group
            return true;
        } else if (c == '+' || c == '*') {
            // Check if we're inside a group that will be quantified
            // Look ahead for group close + quantifier
            size_t temp_depth = depth;
            for (size_t j = i + 1; j < pattern.size() && temp_depth > 0; ++j) {
                if (pattern[j] == '\\' && j + 1 < pattern.size()) {
                    ++j;
                    continue;
                }
                if (pattern[j] == '(') {
                    ++temp_depth;
                } else if (pattern[j] == ')') {
                    --temp_depth;
                    if (temp_depth == depth - 1 && j + 1 < pattern.size()) {
                        char next = pattern[j + 1];
                        if (next == '+' || next == '*') {
                            return true;  // Inner quantifier in outer quantified group
                        }
                    }
                }
            }
        }
    }

    return false;
}

bool PatternValidator::check_nested_star(std::string_view pattern) noexcept {
    // Detect patterns like (a*)*
    // Similar logic to check_nested_plus
    return check_nested_plus(pattern);  // Same detection logic works
}

bool PatternValidator::check_alternation_in_quantified_group(std::string_view pattern) noexcept {
    // Detect patterns like (a|aa)+, (x|xy)*
    size_t depth         = 0;
    bool has_alternation = false;
    size_t group_start   = 0;

    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];

        if (c == '\\' && i + 1 < pattern.size()) {
            ++i;
            continue;
        }

        if (c == '(') {
            if (depth == 0) {
                group_start     = i;
                has_alternation = false;
            }
            ++depth;
        } else if (c == '|' && depth == 1) {
            has_alternation = true;
        } else if (c == ')') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && has_alternation) {
                    // Check if followed by quantifier
                    if (i + 1 < pattern.size()) {
                        char next = pattern[i + 1];
                        if (next == '+' || next == '*') {
                            // Check if alternations could overlap
                            // Simple heuristic: if group is small, likely overlap
                            if ((i - group_start) < 20) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

size_t PatternValidator::calculate_complexity(std::string_view pattern) noexcept {
    size_t complexity = 0;

    complexity += count_quantifiers(pattern);
    complexity += count_groups(pattern) * 2;

    if (has_nested_quantifiers(pattern)) {
        complexity += 20;  // Heavy penalty
    }

    // Backreferences
    if (pattern.find("\\1") != std::string_view::npos)
        complexity += 5;
    if (pattern.find("\\2") != std::string_view::npos)
        complexity += 5;

    // Lookahead/lookbehind
    if (pattern.find("(?=") != std::string_view::npos)
        complexity += 3;
    if (pattern.find("(?!") != std::string_view::npos)
        complexity += 3;
    if (pattern.find("(?<=") != std::string_view::npos)
        complexity += 5;
    if (pattern.find("(?<!") != std::string_view::npos)
        complexity += 5;

    return complexity;
}

size_t PatternValidator::count_quantifiers(std::string_view pattern) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '\\' && i + 1 < pattern.size()) {
            ++i;
            continue;
        }
        if (pattern[i] == '+' || pattern[i] == '*' || pattern[i] == '?') {
            ++count;
        } else if (pattern[i] == '{') {
            ++count;
        }
    }
    return count;
}

size_t PatternValidator::count_groups(std::string_view pattern) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '\\' && i + 1 < pattern.size()) {
            ++i;
            continue;
        }
        if (pattern[i] == '(') {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// CompiledPatternCache Implementation
// ============================================================================

CompiledPatternCache::CompiledPatternCache() : CompiledPatternCache(PatternCacheConfig{}) {}

CompiledPatternCache::CompiledPatternCache(const PatternCacheConfig& config) : config_(config) {
    IPB_LOG_DEBUG(LOG_CAT, "Pattern cache created with max_size=" << config.max_size);
}

CompiledPatternCache::~CompiledPatternCache() = default;

CompiledPatternCache::CompiledPatternCache(CompiledPatternCache&& other) noexcept {
    std::unique_lock lock(other.mutex_);
    config_   = std::move(other.config_);
    cache_    = std::move(other.cache_);
    lru_list_ = std::move(other.lru_list_);
}

CompiledPatternCache& CompiledPatternCache::operator=(CompiledPatternCache&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(mutex_, other.mutex_);
        config_   = std::move(other.config_);
        cache_    = std::move(other.cache_);
        lru_list_ = std::move(other.lru_list_);
    }
    return *this;
}

const std::regex* CompiledPatternCache::get(std::string_view pattern) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = cache_.find(std::string(pattern));
    if (it != cache_.end()) {
        stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
        it->second.first.use_count.fetch_add(1, std::memory_order_relaxed);
        touch_lru(it->first);
        return it->second.first.compiled.get();
    }

    stats_.cache_misses.fetch_add(1, std::memory_order_relaxed);
    return nullptr;
}

Result<const std::regex*> CompiledPatternCache::get_or_compile(std::string_view pattern) {
    // Fast path: check cache with shared lock
    {
        std::shared_lock lock(mutex_);
        auto it = cache_.find(std::string(pattern));
        if (it != cache_.end()) {
            stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
            it->second.first.use_count.fetch_add(1, std::memory_order_relaxed);
            touch_lru(it->first);
            return ok(static_cast<const std::regex*>(it->second.first.compiled.get()));
        }
    }

    // Slow path: compile with exclusive lock
    std::unique_lock lock(mutex_);

    // Double-check after acquiring exclusive lock
    auto it = cache_.find(std::string(pattern));
    if (it != cache_.end()) {
        stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
        return ok(static_cast<const std::regex*>(it->second.first.compiled.get()));
    }

    stats_.cache_misses.fetch_add(1, std::memory_order_relaxed);

    // Compile pattern
    auto compile_result = compile_pattern(pattern);
    if (!compile_result) {
        return err<const std::regex*>(compile_result.error());
    }

    // Evict if necessary
    while (cache_.size() >= config_.max_size) {
        evict_lru();
    }

    // Insert into cache
    std::string pattern_str(pattern);
    lru_list_.push_front(pattern_str);

    auto& entry  = cache_[pattern_str];
    entry.first  = std::move(compile_result.value());
    entry.second = lru_list_.begin();

    IPB_LOG_DEBUG(LOG_CAT, "Compiled and cached pattern: " << pattern);

    return ok(static_cast<const std::regex*>(entry.first.compiled.get()));
}

Result<void> CompiledPatternCache::precompile(std::string_view pattern) {
    auto result = get_or_compile(pattern);
    if (!result) {
        return err(result.error());
    }
    return ok();
}

PatternValidationResult CompiledPatternCache::validate(std::string_view pattern) const noexcept {
    if (!config_.enable_validation) {
        return PatternValidationResult{true, "Validation disabled", 0, false, false, 0};
    }
    return PatternValidator::validate(pattern, config_.max_complexity);
}

bool CompiledPatternCache::remove(std::string_view pattern) {
    std::unique_lock lock(mutex_);

    auto it = cache_.find(std::string(pattern));
    if (it != cache_.end()) {
        lru_list_.erase(it->second.second);
        cache_.erase(it);
        return true;
    }
    return false;
}

void CompiledPatternCache::clear() {
    std::unique_lock lock(mutex_);
    cache_.clear();
    lru_list_.clear();
    IPB_LOG_DEBUG(LOG_CAT, "Pattern cache cleared");
}

size_t CompiledPatternCache::size() const noexcept {
    std::shared_lock lock(mutex_);
    return cache_.size();
}

bool CompiledPatternCache::contains(std::string_view pattern) const noexcept {
    std::shared_lock lock(mutex_);
    return cache_.find(std::string(pattern)) != cache_.end();
}

CompiledPatternCache& CompiledPatternCache::global_instance() {
    static CompiledPatternCache instance;
    return instance;
}

Result<CachedPattern> CompiledPatternCache::compile_pattern(std::string_view pattern) {
    // Validate input
    if (pattern.empty()) {
        stats_.compilation_failures.fetch_add(1, std::memory_order_relaxed);
        return err<CachedPattern>(ErrorCode::INVALID_ARGUMENT, "Pattern cannot be empty");
    }

    if (pattern.size() > config_.max_pattern_length) {
        stats_.compilation_failures.fetch_add(1, std::memory_order_relaxed);
        return err<CachedPattern>(ErrorCode::INVALID_ARGUMENT,
                                  "Pattern too long: " + std::to_string(pattern.size()) + " > " +
                                      std::to_string(config_.max_pattern_length));
    }

    // Validate pattern safety
    if (config_.enable_validation) {
        auto validation = PatternValidator::validate(pattern, config_.max_complexity);
        if (!validation.is_safe) {
            stats_.validation_rejections.fetch_add(1, std::memory_order_relaxed);
            IPB_LOG_WARN(LOG_CAT, "Pattern validation failed: " << validation.reason
                                                                << " for pattern: " << pattern);
            return err<CachedPattern>(ErrorCode::PATTERN_INVALID, validation.reason);
        }
    }

    // Compile with timeout protection
    auto start = std::chrono::steady_clock::now();

    if (config_.compilation_timeout.count() > 0) {
        // Compile in separate thread with timeout
        std::promise<std::unique_ptr<std::regex>> promise;
        auto future = promise.get_future();

        std::thread compile_thread([&promise, &pattern, this]() {
            try {
                auto regex =
                    std::make_unique<std::regex>(std::string(pattern), config_.regex_flags);
                promise.set_value(std::move(regex));
            } catch (const std::regex_error&) {
                promise.set_exception(std::current_exception());
            }
        });

        auto status = future.wait_for(config_.compilation_timeout);

        if (status == std::future_status::timeout) {
            // Timeout - detach thread (will complete eventually)
            compile_thread.detach();
            stats_.timeout_rejections.fetch_add(1, std::memory_order_relaxed);
            IPB_LOG_WARN(LOG_CAT, "Pattern compilation timeout: " << pattern);
            return err<CachedPattern>(ErrorCode::OPERATION_TIMEOUT,
                                      "Pattern compilation exceeded " +
                                          std::to_string(config_.compilation_timeout.count()) +
                                          "ms");
        }

        compile_thread.join();

        try {
            auto compiled = future.get();
            auto end      = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

            stats_.compilations.fetch_add(1, std::memory_order_relaxed);
            stats_.total_compilation_time_ns.fetch_add(duration.count(), std::memory_order_relaxed);

            CachedPattern result;
            result.pattern_string   = std::string(pattern);
            result.compiled         = std::move(compiled);
            result.compiled_at      = std::chrono::steady_clock::now();
            result.compilation_time = duration;
            result.complexity_score = PatternValidator::calculate_complexity(pattern);

            return ok(std::move(result));

        } catch (const std::regex_error& e) {
            stats_.compilation_failures.fetch_add(1, std::memory_order_relaxed);
            IPB_LOG_WARN(LOG_CAT,
                         "Pattern compilation failed: " << e.what() << " for pattern: " << pattern);
            return err<CachedPattern>(ErrorCode::PATTERN_INVALID,
                                      std::string("Regex compilation error: ") + e.what());
        }
    } else {
        // No timeout - compile directly
        try {
            auto compiled = std::make_unique<std::regex>(std::string(pattern), config_.regex_flags);

            auto end      = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

            stats_.compilations.fetch_add(1, std::memory_order_relaxed);
            stats_.total_compilation_time_ns.fetch_add(duration.count(), std::memory_order_relaxed);

            CachedPattern result;
            result.pattern_string   = std::string(pattern);
            result.compiled         = std::move(compiled);
            result.compiled_at      = std::chrono::steady_clock::now();
            result.compilation_time = duration;
            result.complexity_score = PatternValidator::calculate_complexity(pattern);

            return ok(std::move(result));

        } catch (const std::regex_error& e) {
            stats_.compilation_failures.fetch_add(1, std::memory_order_relaxed);
            return err<CachedPattern>(ErrorCode::PATTERN_INVALID,
                                      std::string("Regex compilation error: ") + e.what());
        }
    }
}

void CompiledPatternCache::evict_lru() {
    if (lru_list_.empty())
        return;

    std::string victim = lru_list_.back();
    lru_list_.pop_back();
    cache_.erase(victim);
    stats_.evictions.fetch_add(1, std::memory_order_relaxed);

    IPB_LOG_DEBUG(LOG_CAT, "Evicted pattern from cache: " << victim);
}

void CompiledPatternCache::touch_lru(const std::string& pattern) const {
    // Move to front of LRU list
    auto it = cache_.find(pattern);
    if (it != cache_.end() && it->second.second != lru_list_.begin()) {
        lru_list_.erase(it->second.second);
        lru_list_.push_front(pattern);
        it->second.second = lru_list_.begin();
    }
}

// ============================================================================
// CachedPatternMatcher Implementation
// ============================================================================

CachedPatternMatcher::CachedPatternMatcher(std::string_view pattern)
    : CachedPatternMatcher(pattern, CompiledPatternCache::global_instance()) {}

CachedPatternMatcher::CachedPatternMatcher(std::string_view pattern, CompiledPatternCache& cache)
    : pattern_(pattern) {
    auto result = cache.get_or_compile(pattern);
    if (result) {
        compiled_ = result.value();
    } else {
        error_ = result.message();
    }
}

bool CachedPatternMatcher::matches(std::string_view input) const noexcept {
    if (!compiled_)
        return false;

    try {
        return std::regex_match(input.begin(), input.end(), *compiled_);
    } catch (...) {
        // Should never happen with pre-validated patterns
        return false;
    }
}

std::optional<std::vector<std::string>> CachedPatternMatcher::match_groups(
    std::string_view input) const {
    if (!compiled_)
        return std::nullopt;

    try {
        std::match_results<std::string_view::const_iterator> match;
        if (std::regex_match(input.begin(), input.end(), match, *compiled_)) {
            std::vector<std::string> groups;
            groups.reserve(match.size());
            for (const auto& m : match) {
                groups.emplace_back(m.first, m.second);
            }
            return groups;
        }
    } catch (...) {
        // Ignore exceptions
    }

    return std::nullopt;
}

}  // namespace ipb::core
