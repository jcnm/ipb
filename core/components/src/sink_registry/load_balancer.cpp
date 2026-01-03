#include "ipb/core/sink_registry/load_balancer.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>

namespace ipb::core {

// ============================================================================
// RoundRobinBalancer
// ============================================================================

std::vector<std::string> RoundRobinBalancer::select(
    const std::vector<const SinkInfo*>& candidates) {
    if (candidates.empty()) {
        return {};
    }

    // Filter enabled and healthy sinks
    std::vector<const SinkInfo*> available;
    for (const auto* sink : candidates) {
        if (sink->enabled && sink->health != SinkHealth::UNHEALTHY) {
            available.push_back(sink);
        }
    }

    if (available.empty()) {
        return {};
    }

    size_t index = counter_.fetch_add(1, std::memory_order_relaxed) % available.size();
    return {available[index]->id};
}

// ============================================================================
// WeightedRoundRobinBalancer
// ============================================================================

std::vector<std::string> WeightedRoundRobinBalancer::select(
    const std::vector<const SinkInfo*>& candidates) {
    if (candidates.empty()) {
        return {};
    }

    // Filter and compute total weight
    std::vector<const SinkInfo*> available;
    uint64_t total_weight = 0;

    for (const auto* sink : candidates) {
        if (sink->enabled && sink->health != SinkHealth::UNHEALTHY) {
            available.push_back(sink);
            total_weight += sink->weight;
        }
    }

    if (available.empty() || total_weight == 0) {
        return {};
    }

    // Weighted selection
    uint64_t counter    = counter_.fetch_add(1, std::memory_order_relaxed);
    uint64_t target     = counter % total_weight;
    uint64_t cumulative = 0;

    for (const auto* sink : available) {
        cumulative += sink->weight;
        if (target < cumulative) {
            return {sink->id};
        }
    }

    // Fallback to last
    return {available.back()->id};
}

// ============================================================================
// LeastConnectionsBalancer
// ============================================================================

std::vector<std::string> LeastConnectionsBalancer::select(
    const std::vector<const SinkInfo*>& candidates) {
    if (candidates.empty()) {
        return {};
    }

    const SinkInfo* best = nullptr;
    int64_t min_pending  = INT64_MAX;

    for (const auto* sink : candidates) {
        if (!sink->enabled || sink->health == SinkHealth::UNHEALTHY) {
            continue;
        }

        int64_t pending = sink->pending_count.load(std::memory_order_relaxed);
        if (pending < min_pending) {
            min_pending = pending;
            best        = sink;
        }
    }

    return best ? std::vector<std::string>{best->id} : std::vector<std::string>{};
}

// ============================================================================
// LeastLatencyBalancer
// ============================================================================

std::vector<std::string> LeastLatencyBalancer::select(
    const std::vector<const SinkInfo*>& candidates) {
    if (candidates.empty()) {
        return {};
    }

    const SinkInfo* best = nullptr;
    double min_latency   = std::numeric_limits<double>::max();

    for (const auto* sink : candidates) {
        if (!sink->enabled || sink->health == SinkHealth::UNHEALTHY) {
            continue;
        }

        double latency = sink->avg_latency_us();
        // Consider sinks with no data (latency = 0) as good candidates
        if (latency < min_latency || (latency == 0 && best == nullptr)) {
            min_latency = latency;
            best        = sink;
        }
    }

    return best ? std::vector<std::string>{best->id} : std::vector<std::string>{};
}

// ============================================================================
// HashBasedBalancer
// ============================================================================

std::vector<std::string> HashBasedBalancer::select(const std::vector<const SinkInfo*>& candidates) {
    // Without context, fall back to round-robin style
    if (candidates.empty()) {
        return {};
    }

    std::vector<const SinkInfo*> available;
    for (const auto* sink : candidates) {
        if (sink->enabled && sink->health != SinkHealth::UNHEALTHY) {
            available.push_back(sink);
        }
    }

    if (available.empty()) {
        return {};
    }

    // Use current time as hash input for deterministic but varying selection
    size_t hash  = static_cast<size_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    size_t index = hash % available.size();

    return {available[index]->id};
}

std::vector<std::string> HashBasedBalancer::select(const std::vector<const SinkInfo*>& candidates,
                                                   const common::DataPoint& context) {
    if (candidates.empty()) {
        return {};
    }

    std::vector<const SinkInfo*> available;
    for (const auto* sink : candidates) {
        if (sink->enabled && sink->health != SinkHealth::UNHEALTHY) {
            available.push_back(sink);
        }
    }

    if (available.empty()) {
        return {};
    }

    // Hash based on address for consistent routing
    size_t hash  = compute_hash(context.address());
    size_t index = hash % available.size();

    return {available[index]->id};
}

size_t HashBasedBalancer::compute_hash(std::string_view address) const noexcept {
    // FNV-1a hash for good distribution
    size_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (char c : address) {
        hash ^= static_cast<size_t>(c);
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}

// ============================================================================
// RandomBalancer
// ============================================================================

RandomBalancer::RandomBalancer() : rng_(std::random_device{}()) {}

std::vector<std::string> RandomBalancer::select(const std::vector<const SinkInfo*>& candidates) {
    if (candidates.empty()) {
        return {};
    }

    std::vector<const SinkInfo*> available;
    for (const auto* sink : candidates) {
        if (sink->enabled && sink->health != SinkHealth::UNHEALTHY) {
            available.push_back(sink);
        }
    }

    if (available.empty()) {
        return {};
    }

    std::lock_guard lock(rng_mutex_);
    std::uniform_int_distribution<size_t> dist(0, available.size() - 1);
    size_t index = dist(rng_);

    return {available[index]->id};
}

// ============================================================================
// FailoverBalancer
// ============================================================================

std::vector<std::string> FailoverBalancer::select(const std::vector<const SinkInfo*>& candidates) {
    if (candidates.empty()) {
        return {};
    }

    // Sort by priority (lower = higher priority)
    std::vector<const SinkInfo*> sorted = candidates;
    std::sort(sorted.begin(), sorted.end(),
              [](const SinkInfo* a, const SinkInfo* b) { return a->priority < b->priority; });

    // Return first healthy sink
    for (const auto* sink : sorted) {
        if (sink->enabled && sink->health != SinkHealth::UNHEALTHY) {
            return {sink->id};
        }
    }

    // No healthy sinks - return first enabled (even if degraded)
    for (const auto* sink : sorted) {
        if (sink->enabled) {
            return {sink->id};
        }
    }

    return {};
}

// ============================================================================
// BroadcastBalancer
// ============================================================================

std::vector<std::string> BroadcastBalancer::select(const std::vector<const SinkInfo*>& candidates) {
    std::vector<std::string> result;

    for (const auto* sink : candidates) {
        if (sink->enabled && sink->health != SinkHealth::UNHEALTHY) {
            result.push_back(sink->id);
        }
    }

    return result;
}

// ============================================================================
// LoadBalancerFactory
// ============================================================================

std::unique_ptr<ILoadBalancer> LoadBalancerFactory::create(LoadBalanceStrategy strategy) {
    switch (strategy) {
        case LoadBalanceStrategy::ROUND_ROBIN:
            return std::make_unique<RoundRobinBalancer>();
        case LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN:
            return std::make_unique<WeightedRoundRobinBalancer>();
        case LoadBalanceStrategy::LEAST_CONNECTIONS:
            return std::make_unique<LeastConnectionsBalancer>();
        case LoadBalanceStrategy::LEAST_LATENCY:
            return std::make_unique<LeastLatencyBalancer>();
        case LoadBalanceStrategy::HASH_BASED:
            return std::make_unique<HashBasedBalancer>();
        case LoadBalanceStrategy::RANDOM:
            return std::make_unique<RandomBalancer>();
        case LoadBalanceStrategy::FAILOVER:
            return std::make_unique<FailoverBalancer>();
        case LoadBalanceStrategy::BROADCAST:
            return std::make_unique<BroadcastBalancer>();
        default:
            return std::make_unique<RoundRobinBalancer>();
    }
}

}  // namespace ipb::core
