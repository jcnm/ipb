#pragma once

/**
 * @file load_balancer.hpp
 * @brief Load balancing algorithms for sink selection
 */

#include <ipb/common/data_point.hpp>

#include <atomic>
#include <functional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "sink_registry.hpp"

namespace ipb::core {

/**
 * @brief Abstract load balancer interface
 */
class ILoadBalancer {
public:
    virtual ~ILoadBalancer() = default;

    /// Select sink(s) from candidates
    virtual std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) = 0;

    /// Select with data point context (for hash-based strategies)
    virtual std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates,
                                            const common::DataPoint& context) {
        return select(candidates);  // Default ignores context
    }

    /// Get strategy type
    virtual LoadBalanceStrategy strategy() const noexcept = 0;
};

/**
 * @brief Round-robin load balancer
 */
class RoundRobinBalancer : public ILoadBalancer {
public:
    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    LoadBalanceStrategy strategy() const noexcept override {
        return LoadBalanceStrategy::ROUND_ROBIN;
    }

private:
    std::atomic<uint64_t> counter_{0};
};

/**
 * @brief Weighted round-robin load balancer
 */
class WeightedRoundRobinBalancer : public ILoadBalancer {
public:
    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    LoadBalanceStrategy strategy() const noexcept override {
        return LoadBalanceStrategy::WEIGHTED_ROUND_ROBIN;
    }

private:
    std::atomic<uint64_t> counter_{0};
};

/**
 * @brief Least connections load balancer
 */
class LeastConnectionsBalancer : public ILoadBalancer {
public:
    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    LoadBalanceStrategy strategy() const noexcept override {
        return LoadBalanceStrategy::LEAST_CONNECTIONS;
    }
};

/**
 * @brief Least latency load balancer
 */
class LeastLatencyBalancer : public ILoadBalancer {
public:
    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    LoadBalanceStrategy strategy() const noexcept override {
        return LoadBalanceStrategy::LEAST_LATENCY;
    }
};

/**
 * @brief Hash-based consistent load balancer
 */
class HashBasedBalancer : public ILoadBalancer {
public:
    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates,
                                    const common::DataPoint& context) override;

    LoadBalanceStrategy strategy() const noexcept override {
        return LoadBalanceStrategy::HASH_BASED;
    }

private:
    /// Compute hash for data point address
    size_t compute_hash(std::string_view address) const noexcept;
};

/**
 * @brief Random load balancer
 */
class RandomBalancer : public ILoadBalancer {
public:
    RandomBalancer();

    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    LoadBalanceStrategy strategy() const noexcept override { return LoadBalanceStrategy::RANDOM; }

private:
    std::mt19937 rng_;
    std::mutex rng_mutex_;
};

/**
 * @brief Failover load balancer (primary with backups)
 */
class FailoverBalancer : public ILoadBalancer {
public:
    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    LoadBalanceStrategy strategy() const noexcept override { return LoadBalanceStrategy::FAILOVER; }
};

/**
 * @brief Broadcast load balancer (selects all)
 */
class BroadcastBalancer : public ILoadBalancer {
public:
    std::vector<std::string> select(const std::vector<const SinkInfo*>& candidates) override;

    LoadBalanceStrategy strategy() const noexcept override {
        return LoadBalanceStrategy::BROADCAST;
    }
};

/**
 * @brief Factory for creating load balancers
 */
class LoadBalancerFactory {
public:
    static std::unique_ptr<ILoadBalancer> create(LoadBalanceStrategy strategy);
};

}  // namespace ipb::core
