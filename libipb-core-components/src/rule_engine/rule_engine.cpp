#include "ipb/core/rule_engine/rule_engine.hpp"
#include "ipb/core/rule_engine/pattern_matcher.hpp"
#include <ipb/common/endpoint.hpp>

#include <algorithm>
#include <shared_mutex>
#include <unordered_map>

namespace ipb::core {

// ============================================================================
// ValueCondition Implementation
// ============================================================================

bool ValueCondition::evaluate(const common::Value& value) const noexcept {
    if (value.empty()) {
        return false;
    }

    // Extract numeric value from DataPoint value
    auto extract_double = [](const common::Value& v) -> std::optional<double> {
        switch (v.type()) {
            case common::Value::Type::BOOL:
                return v.get<bool>() ? 1.0 : 0.0;
            case common::Value::Type::INT8:
                return static_cast<double>(v.get<int8_t>());
            case common::Value::Type::INT16:
                return static_cast<double>(v.get<int16_t>());
            case common::Value::Type::INT32:
                return static_cast<double>(v.get<int32_t>());
            case common::Value::Type::INT64:
                return static_cast<double>(v.get<int64_t>());
            case common::Value::Type::UINT8:
                return static_cast<double>(v.get<uint8_t>());
            case common::Value::Type::UINT16:
                return static_cast<double>(v.get<uint16_t>());
            case common::Value::Type::UINT32:
                return static_cast<double>(v.get<uint32_t>());
            case common::Value::Type::UINT64:
                return static_cast<double>(v.get<uint64_t>());
            case common::Value::Type::FLOAT32:
                return static_cast<double>(v.get<float>());
            case common::Value::Type::FLOAT64:
                return v.get<double>();
            default:
                return std::nullopt;
        }
    };

    // Get reference value as double
    double ref = 0.0;
    double ref_high = 0.0;

    std::visit([&ref](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_arithmetic_v<T>) {
            ref = static_cast<double>(arg);
        }
    }, reference);

    if (op == CompareOp::BETWEEN) {
        std::visit([&ref_high](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_arithmetic_v<T>) {
                ref_high = static_cast<double>(arg);
            }
        }, reference_high);
    }

    auto val_opt = extract_double(value);
    if (!val_opt) {
        // String comparison for non-numeric types
        if (value.type() == common::Value::Type::STRING) {
            auto sv = value.as_string_view();
            std::string val_str(sv);

            if (auto* ref_str = std::get_if<std::string>(&reference)) {
                switch (op) {
                    case CompareOp::EQ:
                        return val_str == *ref_str;
                    case CompareOp::NE:
                        return val_str != *ref_str;
                    case CompareOp::LT:
                        return val_str < *ref_str;
                    case CompareOp::LE:
                        return val_str <= *ref_str;
                    case CompareOp::GT:
                        return val_str > *ref_str;
                    case CompareOp::GE:
                        return val_str >= *ref_str;
                    default:
                        return false;
                }
            }
        }
        return false;
    }

    double val = *val_opt;

    switch (op) {
        case CompareOp::EQ:
            return val == ref;
        case CompareOp::NE:
            return val != ref;
        case CompareOp::LT:
            return val < ref;
        case CompareOp::LE:
            return val <= ref;
        case CompareOp::GT:
            return val > ref;
        case CompareOp::GE:
            return val >= ref;
        case CompareOp::BETWEEN:
            return val >= ref && val <= ref_high;
    }

    return false;
}

// ============================================================================
// RoutingRule Implementation
// ============================================================================

RuleMatchResult RoutingRule::evaluate(const common::DataPoint& dp) const {
    common::rt::HighResolutionTimer timer;

    RuleMatchResult result;
    result.rule_id = id;
    result.priority = priority;
    result.target_ids = target_sink_ids;

    eval_count.fetch_add(1, std::memory_order_relaxed);

    if (!enabled) {
        return result;
    }

    bool matched = false;

    switch (type) {
        case RuleType::STATIC:
            for (const auto& addr : source_addresses) {
                if (dp.address() == addr) {
                    matched = true;
                    break;
                }
            }
            break;

        case RuleType::PATTERN: {
            auto matcher = PatternMatcherFactory::create(address_pattern);
            auto match_result = matcher->match_with_groups(dp.address());
            matched = match_result.matched;
            result.captured_groups = std::move(match_result.captured_groups);
            break;
        }

        case RuleType::PROTOCOL:
            for (uint16_t proto : protocol_ids) {
                if (dp.protocol_id() == proto) {
                    matched = true;
                    break;
                }
            }
            break;

        case RuleType::QUALITY:
            for (auto q : quality_levels) {
                if (dp.quality() == q) {
                    matched = true;
                    break;
                }
            }
            break;

        case RuleType::VALUE:
            if (value_condition) {
                matched = value_condition->evaluate(dp.value());
            }
            break;

        case RuleType::TIMESTAMP:
            matched = dp.timestamp() >= start_time && dp.timestamp() <= end_time;
            break;

        case RuleType::CUSTOM:
            if (custom_predicate) {
                matched = custom_predicate(dp);
            }
            break;

        case RuleType::COMPOSITE:
            // TODO: Implement composite rule evaluation
            break;
    }

    result.matched = matched;

    if (matched) {
        match_count.fetch_add(1, std::memory_order_relaxed);
    }

    auto elapsed = timer.elapsed();
    total_eval_time_ns.fetch_add(elapsed.count(), std::memory_order_relaxed);

    return result;
}

// ============================================================================
// RuleEngineImpl - Private Implementation
// ============================================================================

class RuleEngineImpl {
public:
    explicit RuleEngineImpl(const RuleEngineConfig& config)
        : config_(config) {}

    uint32_t add_rule(RoutingRule rule) {
        std::unique_lock lock(rules_mutex_);

        uint32_t id = next_rule_id_++;
        rule.id = id;

        // Pre-compile pattern if configured
        if (config_.precompile_patterns && rule.type == RuleType::PATTERN) {
            compiled_patterns_[id] = PatternMatcherFactory::create(
                rule.address_pattern,
                config_.prefer_ctre ?
                    PatternMatcherFactory::MatcherType::REGEX_CTRE :
                    PatternMatcherFactory::MatcherType::AUTO);
        }

        rules_.push_back(std::move(rule));

        // Re-sort by priority (descending)
        std::sort(rules_.begin(), rules_.end(),
            [](const RoutingRule& a, const RoutingRule& b) {
                return static_cast<uint8_t>(a.priority) > static_cast<uint8_t>(b.priority);
            });

        // Invalidate cache
        if (config_.enable_cache) {
            cache_.clear();
        }

        return id;
    }

    bool update_rule(uint32_t rule_id, const RoutingRule& rule) {
        std::unique_lock lock(rules_mutex_);

        auto it = std::find_if(rules_.begin(), rules_.end(),
            [rule_id](const RoutingRule& r) { return r.id == rule_id; });

        if (it == rules_.end()) {
            return false;
        }

        *it = rule;
        it->id = rule_id;

        // Update compiled pattern
        if (config_.precompile_patterns && rule.type == RuleType::PATTERN) {
            compiled_patterns_[rule_id] = PatternMatcherFactory::create(
                rule.address_pattern,
                config_.prefer_ctre ?
                    PatternMatcherFactory::MatcherType::REGEX_CTRE :
                    PatternMatcherFactory::MatcherType::AUTO);
        }

        // Re-sort
        std::sort(rules_.begin(), rules_.end(),
            [](const RoutingRule& a, const RoutingRule& b) {
                return static_cast<uint8_t>(a.priority) > static_cast<uint8_t>(b.priority);
            });

        if (config_.enable_cache) {
            cache_.clear();
        }

        return true;
    }

    bool remove_rule(uint32_t rule_id) {
        std::unique_lock lock(rules_mutex_);

        auto it = std::find_if(rules_.begin(), rules_.end(),
            [rule_id](const RoutingRule& r) { return r.id == rule_id; });

        if (it == rules_.end()) {
            return false;
        }

        rules_.erase(it);
        compiled_patterns_.erase(rule_id);

        if (config_.enable_cache) {
            cache_.clear();
        }

        return true;
    }

    bool set_rule_enabled(uint32_t rule_id, bool enabled) {
        std::unique_lock lock(rules_mutex_);

        auto it = std::find_if(rules_.begin(), rules_.end(),
            [rule_id](const RoutingRule& r) { return r.id == rule_id; });

        if (it == rules_.end()) {
            return false;
        }

        it->enabled = enabled;

        if (config_.enable_cache) {
            cache_.clear();
        }

        return true;
    }

    std::optional<RoutingRule> get_rule(uint32_t rule_id) const {
        std::shared_lock lock(rules_mutex_);

        auto it = std::find_if(rules_.begin(), rules_.end(),
            [rule_id](const RoutingRule& r) { return r.id == rule_id; });

        if (it == rules_.end()) {
            return std::nullopt;
        }

        return *it;
    }

    std::vector<RoutingRule> get_all_rules() const {
        std::shared_lock lock(rules_mutex_);
        return rules_;
    }

    void clear_rules() {
        std::unique_lock lock(rules_mutex_);
        rules_.clear();
        compiled_patterns_.clear();
        cache_.clear();
    }

    size_t rule_count() const noexcept {
        std::shared_lock lock(rules_mutex_);
        return rules_.size();
    }

    std::vector<RuleMatchResult> evaluate(const common::DataPoint& dp) {
        common::rt::HighResolutionTimer timer;

        std::vector<RuleMatchResult> results;
        std::string address(dp.address());

        // Check cache first
        if (config_.enable_cache) {
            auto cached = check_cache(address);
            if (cached) {
                stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
                return *cached;
            }
            stats_.cache_misses.fetch_add(1, std::memory_order_relaxed);
        }

        // Evaluate all rules
        {
            std::shared_lock lock(rules_mutex_);

            for (const auto& rule : rules_) {
                if (!rule.enabled) continue;

                auto result = evaluate_rule(rule, dp);
                if (result.matched) {
                    results.push_back(std::move(result));
                    stats_.total_matches.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        // Update cache
        if (config_.enable_cache && results.size() > 0) {
            update_cache(address, results);
        }

        stats_.total_evaluations.fetch_add(1, std::memory_order_relaxed);

        auto elapsed = timer.elapsed();
        update_timing_stats(elapsed.count());

        return results;
    }

    std::optional<RuleMatchResult> evaluate_first(const common::DataPoint& dp) {
        common::rt::HighResolutionTimer timer;

        std::shared_lock lock(rules_mutex_);

        for (const auto& rule : rules_) {
            if (!rule.enabled) continue;

            auto result = evaluate_rule(rule, dp);
            if (result.matched) {
                stats_.total_evaluations.fetch_add(1, std::memory_order_relaxed);
                stats_.total_matches.fetch_add(1, std::memory_order_relaxed);

                auto elapsed = timer.elapsed();
                update_timing_stats(elapsed.count());

                return result;
            }
        }

        stats_.total_evaluations.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    std::vector<RuleMatchResult> evaluate_priority(
            const common::DataPoint& dp,
            RulePriority min_priority) {
        std::vector<RuleMatchResult> results;

        std::shared_lock lock(rules_mutex_);

        for (const auto& rule : rules_) {
            if (!rule.enabled) continue;
            if (static_cast<uint8_t>(rule.priority) < static_cast<uint8_t>(min_priority)) {
                break;  // Rules are sorted by priority, so we can stop here
            }

            auto result = evaluate_rule(rule, dp);
            if (result.matched) {
                results.push_back(std::move(result));
            }
        }

        stats_.total_evaluations.fetch_add(1, std::memory_order_relaxed);
        stats_.total_matches.fetch_add(results.size(), std::memory_order_relaxed);

        return results;
    }

    std::vector<std::vector<RuleMatchResult>> evaluate_batch(
            std::span<const common::DataPoint> data_points) {
        std::vector<std::vector<RuleMatchResult>> results;
        results.reserve(data_points.size());

        for (const auto& dp : data_points) {
            results.push_back(evaluate(dp));
        }

        return results;
    }

    void clear_cache() {
        std::unique_lock lock(cache_mutex_);
        cache_.clear();
    }

    void invalidate_cache(std::string_view address_pattern) {
        std::unique_lock lock(cache_mutex_);

        auto matcher = PatternMatcherFactory::create(address_pattern);

        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (matcher->matches(it->first)) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    const RuleEngineStats& stats() const noexcept {
        return stats_;
    }

    void reset_stats() {
        stats_.reset();
    }

    const RuleEngineConfig& config() const noexcept {
        return config_;
    }

private:
    RuleMatchResult evaluate_rule(const RoutingRule& rule,
                                  const common::DataPoint& dp) const {
        // Use pre-compiled pattern if available
        if (rule.type == RuleType::PATTERN) {
            auto it = compiled_patterns_.find(rule.id);
            if (it != compiled_patterns_.end()) {
                RuleMatchResult result;
                result.rule_id = rule.id;
                result.priority = rule.priority;
                result.target_ids = rule.target_sink_ids;

                auto match = it->second->match_with_groups(dp.address());
                result.matched = match.matched;
                result.captured_groups = std::move(match.captured_groups);

                return result;
            }
        }

        return rule.evaluate(dp);
    }

    std::optional<std::vector<RuleMatchResult>> check_cache(const std::string& address) {
        std::shared_lock lock(cache_mutex_);

        auto it = cache_.find(address);
        if (it == cache_.end()) {
            return std::nullopt;
        }

        // Check TTL
        if (config_.cache_ttl_ms > 0) {
            auto now = common::Timestamp::now();
            auto age_ms = (now - it->second.timestamp).count() / 1000000;
            if (age_ms > config_.cache_ttl_ms) {
                return std::nullopt;
            }
        }

        return it->second.results;
    }

    void update_cache(const std::string& address,
                     const std::vector<RuleMatchResult>& results) {
        std::unique_lock lock(cache_mutex_);

        // Evict if at capacity
        if (cache_.size() >= config_.cache_size) {
            // Simple LRU: remove oldest entry
            common::Timestamp oldest = common::Timestamp::now();
            std::string oldest_key;

            for (const auto& [key, entry] : cache_) {
                if (entry.timestamp < oldest) {
                    oldest = entry.timestamp;
                    oldest_key = key;
                }
            }

            if (!oldest_key.empty()) {
                cache_.erase(oldest_key);
            }
        }

        cache_[address] = CacheEntry{results, common::Timestamp::now()};
    }

    void update_timing_stats(int64_t elapsed_ns) {
        stats_.total_eval_time_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);

        // Update min/max atomically
        int64_t current_min = stats_.min_eval_time_ns.load(std::memory_order_relaxed);
        while (elapsed_ns < current_min &&
               !stats_.min_eval_time_ns.compare_exchange_weak(current_min, elapsed_ns)) {}

        int64_t current_max = stats_.max_eval_time_ns.load(std::memory_order_relaxed);
        while (elapsed_ns > current_max &&
               !stats_.max_eval_time_ns.compare_exchange_weak(current_max, elapsed_ns)) {}
    }

    RuleEngineConfig config_;
    RuleEngineStats stats_;

    mutable std::shared_mutex rules_mutex_;
    std::vector<RoutingRule> rules_;
    std::unordered_map<uint32_t, std::unique_ptr<IPatternMatcher>> compiled_patterns_;
    std::atomic<uint32_t> next_rule_id_{1};

    // Cache
    struct CacheEntry {
        std::vector<RuleMatchResult> results;
        common::Timestamp timestamp;
    };
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;
};

// ============================================================================
// RuleEngine Public Interface
// ============================================================================

RuleEngine::RuleEngine()
    : impl_(std::make_unique<RuleEngineImpl>(RuleEngineConfig{})) {}

RuleEngine::RuleEngine(const RuleEngineConfig& config)
    : impl_(std::make_unique<RuleEngineImpl>(config)) {}

RuleEngine::~RuleEngine() = default;

RuleEngine::RuleEngine(RuleEngine&&) noexcept = default;
RuleEngine& RuleEngine::operator=(RuleEngine&&) noexcept = default;

uint32_t RuleEngine::add_rule(RoutingRule rule) {
    return impl_->add_rule(std::move(rule));
}

bool RuleEngine::update_rule(uint32_t rule_id, const RoutingRule& rule) {
    return impl_->update_rule(rule_id, rule);
}

bool RuleEngine::remove_rule(uint32_t rule_id) {
    return impl_->remove_rule(rule_id);
}

bool RuleEngine::set_rule_enabled(uint32_t rule_id, bool enabled) {
    return impl_->set_rule_enabled(rule_id, enabled);
}

std::optional<RoutingRule> RuleEngine::get_rule(uint32_t rule_id) const {
    return impl_->get_rule(rule_id);
}

std::vector<RoutingRule> RuleEngine::get_all_rules() const {
    return impl_->get_all_rules();
}

void RuleEngine::clear_rules() {
    impl_->clear_rules();
}

size_t RuleEngine::rule_count() const noexcept {
    return impl_->rule_count();
}

std::vector<RuleMatchResult> RuleEngine::evaluate(const common::DataPoint& dp) {
    return impl_->evaluate(dp);
}

std::optional<RuleMatchResult> RuleEngine::evaluate_first(const common::DataPoint& dp) {
    return impl_->evaluate_first(dp);
}

std::vector<RuleMatchResult> RuleEngine::evaluate_priority(
        const common::DataPoint& dp,
        RulePriority min_priority) {
    return impl_->evaluate_priority(dp, min_priority);
}

std::vector<std::vector<RuleMatchResult>> RuleEngine::evaluate_batch(
        std::span<const common::DataPoint> data_points) {
    return impl_->evaluate_batch(data_points);
}

void RuleEngine::clear_cache() {
    impl_->clear_cache();
}

void RuleEngine::invalidate_cache(std::string_view address_pattern) {
    impl_->invalidate_cache(address_pattern);
}

const RuleEngineStats& RuleEngine::stats() const noexcept {
    return impl_->stats();
}

void RuleEngine::reset_stats() {
    impl_->reset_stats();
}

const RuleEngineConfig& RuleEngine::config() const noexcept {
    return impl_->config();
}

} // namespace ipb::core
