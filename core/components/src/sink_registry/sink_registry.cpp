#include "ipb/core/sink_registry/sink_registry.hpp"

#include <ipb/common/debug.hpp>
#include <ipb/common/endpoint.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include "ipb/core/sink_registry/load_balancer.hpp"

namespace ipb::core {

using namespace common::debug;

namespace {
constexpr std::string_view LOG_CAT = category::ROUTER;  // Sinks are part of routing
}  // anonymous namespace

// ============================================================================
// SinkRegistryImpl - Private Implementation
// ============================================================================

class SinkRegistryImpl {
public:
    explicit SinkRegistryImpl(const SinkRegistryConfig& config) : config_(config) {
        // Create load balancers
        for (int i = 0; i <= static_cast<int>(LoadBalanceStrategy::BROADCAST); ++i) {
            auto strategy        = static_cast<LoadBalanceStrategy>(i);
            balancers_[strategy] = LoadBalancerFactory::create(strategy);
        }
    }

    ~SinkRegistryImpl() { stop(); }

    bool start() {
        IPB_SPAN_CAT("SinkRegistry::start", LOG_CAT);

        if (IPB_UNLIKELY(running_.exchange(true))) {
            IPB_LOG_WARN(LOG_CAT, "SinkRegistry already running");
            return false;
        }

        stop_requested_.store(false);

        if (config_.enable_health_check) {
            IPB_LOG_DEBUG(LOG_CAT, "Starting health check thread");
            health_check_thread_ = std::thread([this]() { health_check_loop(); });
        }

        IPB_LOG_INFO(LOG_CAT, "SinkRegistry started");
        return true;
    }

    void stop() {
        IPB_SPAN_CAT("SinkRegistry::stop", LOG_CAT);

        if (IPB_UNLIKELY(!running_.exchange(false))) {
            IPB_LOG_DEBUG(LOG_CAT, "SinkRegistry stop called but not running");
            return;
        }

        IPB_LOG_INFO(LOG_CAT, "Stopping SinkRegistry...");

        stop_requested_.store(true);

        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }

        IPB_LOG_INFO(LOG_CAT, "SinkRegistry stopped");
    }

    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    bool register_sink(std::string_view id, std::shared_ptr<common::IIPBSink> sink,
                       uint32_t weight) {
        IPB_PRECONDITION(!id.empty());
        IPB_PRECONDITION(sink != nullptr);

        std::unique_lock lock(sinks_mutex_);

        std::string id_str(id);
        if (IPB_UNLIKELY(sinks_.find(id_str) != sinks_.end())) {
            IPB_LOG_WARN(LOG_CAT, "Sink already registered: " << id);
            return false;  // Already registered
        }

        auto info    = std::make_shared<SinkInfo>();
        info->id     = id_str;
        info->sink   = std::move(sink);
        info->weight = weight;
        info->type   = std::string(info->sink->sink_type());
        info->health = SinkHealth::UNKNOWN;

        // Capture type before moving info
        std::string sink_type = info->type;
        sinks_[id_str]        = std::move(info);
        stats_.active_sinks.fetch_add(1, std::memory_order_relaxed);

        IPB_LOG_INFO(LOG_CAT, "Registered sink: " << id << " (type=" << sink_type
                                                  << ", weight=" << weight << ")");
        return true;
    }

    bool unregister_sink(std::string_view id) {
        IPB_PRECONDITION(!id.empty());

        std::unique_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (IPB_UNLIKELY(it == sinks_.end())) {
            IPB_LOG_WARN(LOG_CAT, "Cannot unregister unknown sink: " << id);
            return false;
        }

        IPB_LOG_INFO(LOG_CAT, "Unregistering sink: " << id);
        sinks_.erase(it);
        stats_.active_sinks.fetch_sub(1, std::memory_order_relaxed);

        return true;
    }

    bool has_sink(std::string_view id) const {
        std::shared_lock lock(sinks_mutex_);
        return sinks_.find(std::string(id)) != sinks_.end();
    }

    std::shared_ptr<common::IIPBSink> get_sink(std::string_view id) {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it == sinks_.end()) {
            return nullptr;
        }

        return it->second->sink;
    }

    std::optional<SinkInfo> get_sink_info(std::string_view id) const {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it == sinks_.end()) {
            return std::nullopt;
        }

        // Copy SinkInfo (excluding atomic values)
        SinkInfo copy;
        copy.id                = it->second->id;
        copy.type              = it->second->type;
        copy.sink              = it->second->sink;
        copy.weight            = it->second->weight;
        copy.enabled           = it->second->enabled;
        copy.priority          = it->second->priority;
        copy.health            = it->second->health;
        copy.last_health_check = it->second->last_health_check;
        copy.health_message    = it->second->health_message;

        return copy;
    }

    std::vector<std::string> get_sink_ids() const {
        std::shared_lock lock(sinks_mutex_);

        std::vector<std::string> ids;
        ids.reserve(sinks_.size());

        for (const auto& [id, _] : sinks_) {
            ids.push_back(id);
        }

        return ids;
    }

    size_t sink_count() const noexcept {
        std::shared_lock lock(sinks_mutex_);
        return sinks_.size();
    }

    bool set_sink_enabled(std::string_view id, bool enabled) {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it == sinks_.end()) {
            return false;
        }

        it->second->enabled = enabled;
        return true;
    }

    bool set_sink_weight(std::string_view id, uint32_t weight) {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it == sinks_.end()) {
            return false;
        }

        it->second->weight = weight;
        return true;
    }

    bool set_sink_priority(std::string_view id, uint32_t priority) {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it == sinks_.end()) {
            return false;
        }

        it->second->priority = priority;
        return true;
    }

    SinkSelectionResult select_sink(const std::vector<std::string>& candidate_ids,
                                    LoadBalanceStrategy strategy) {
        SinkSelectionResult result;
        stats_.total_selections.fetch_add(1, std::memory_order_relaxed);

        // Gather candidate SinkInfo pointers
        std::vector<const SinkInfo*> candidates;
        {
            std::shared_lock lock(sinks_mutex_);

            for (const auto& id : candidate_ids) {
                auto it = sinks_.find(id);
                if (it != sinks_.end()) {
                    candidates.push_back(it->second.get());
                }
            }
        }

        if (candidates.empty()) {
            result.error_message = "No valid candidates found";
            stats_.failed_selections.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        // Select using balancer
        auto& balancer           = balancers_[strategy];
        result.selected_sink_ids = balancer->select(candidates);

        if (result.selected_sink_ids.empty()) {
            result.error_message = "No healthy sinks available";
            stats_.failed_selections.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        result.success = true;
        stats_.successful_selections.fetch_add(1, std::memory_order_relaxed);

        return result;
    }

    SinkSelectionResult select_sink(const std::vector<std::string>& candidate_ids,
                                    const common::DataPoint& data_point,
                                    LoadBalanceStrategy strategy) {
        SinkSelectionResult result;
        stats_.total_selections.fetch_add(1, std::memory_order_relaxed);

        // Gather candidate SinkInfo pointers
        std::vector<const SinkInfo*> candidates;
        {
            std::shared_lock lock(sinks_mutex_);

            for (const auto& id : candidate_ids) {
                auto it = sinks_.find(id);
                if (it != sinks_.end()) {
                    candidates.push_back(it->second.get());
                }
            }
        }

        if (candidates.empty()) {
            result.error_message = "No valid candidates found";
            stats_.failed_selections.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        // Select using balancer with context
        auto& balancer           = balancers_[strategy];
        result.selected_sink_ids = balancer->select(candidates, data_point);

        if (result.selected_sink_ids.empty()) {
            result.error_message = "No healthy sinks available";
            stats_.failed_selections.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        result.success = true;
        stats_.successful_selections.fetch_add(1, std::memory_order_relaxed);

        return result;
    }

    SinkSelectionResult select_sink_filtered(const std::vector<std::string>& candidate_ids,
                                             std::function<bool(const SinkInfo&)> filter,
                                             LoadBalanceStrategy strategy) {
        SinkSelectionResult result;
        stats_.total_selections.fetch_add(1, std::memory_order_relaxed);

        // Gather filtered candidate SinkInfo pointers
        std::vector<const SinkInfo*> candidates;
        {
            std::shared_lock lock(sinks_mutex_);

            for (const auto& id : candidate_ids) {
                auto it = sinks_.find(id);
                if (it != sinks_.end() && filter(*it->second)) {
                    candidates.push_back(it->second.get());
                }
            }
        }

        if (candidates.empty()) {
            result.error_message = "No candidates passed filter";
            stats_.failed_selections.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        auto& balancer           = balancers_[strategy];
        result.selected_sink_ids = balancer->select(candidates);

        if (result.selected_sink_ids.empty()) {
            result.error_message = "No healthy sinks available";
            stats_.failed_selections.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        result.success = true;
        stats_.successful_selections.fetch_add(1, std::memory_order_relaxed);

        return result;
    }

    common::Result<> write_to_sink(std::string_view sink_id, const common::DataPoint& data_point) {
        IPB_PRECONDITION(!sink_id.empty());

        std::shared_ptr<SinkInfo> info;

        {
            std::shared_lock lock(sinks_mutex_);

            auto it = sinks_.find(std::string(sink_id));
            if (IPB_UNLIKELY(it == sinks_.end())) {
                IPB_LOG_WARN(LOG_CAT, "Write to unknown sink: " << sink_id);
                return common::Result<>(common::ErrorCode::INVALID_ARGUMENT, "Sink not found");
            }

            info = it->second;
        }

        if (IPB_UNLIKELY(!info->enabled)) {
            IPB_LOG_DEBUG(LOG_CAT, "Write to disabled sink: " << sink_id);
            return common::Result<>(common::ErrorCode::INVALID_ARGUMENT, "Sink is disabled");
        }

        info->pending_count.fetch_add(1, std::memory_order_relaxed);

        IPB_LOG_TRACE(LOG_CAT,
                      "Writing to sink: " << sink_id << " address=" << data_point.address());

        common::rt::HighResolutionTimer timer;
        auto result  = info->sink->write(data_point);
        auto elapsed = timer.elapsed();

        info->pending_count.fetch_sub(1, std::memory_order_relaxed);

        if (IPB_LIKELY(result.is_success())) {
            info->messages_sent.fetch_add(1, std::memory_order_relaxed);
            info->total_latency_ns.fetch_add(elapsed.count(), std::memory_order_relaxed);
        } else {
            info->messages_failed.fetch_add(1, std::memory_order_relaxed);
            update_sink_health_on_failure(info);
            IPB_LOG_WARN(LOG_CAT,
                         "Write to sink " << sink_id << " failed: " << result.error_message());
        }

        return result;
    }

    common::Result<> write_batch_to_sink(std::string_view sink_id,
                                         std::span<const common::DataPoint> batch) {
        std::shared_ptr<SinkInfo> info;

        {
            std::shared_lock lock(sinks_mutex_);

            auto it = sinks_.find(std::string(sink_id));
            if (it == sinks_.end()) {
                return common::Result<>(common::ErrorCode::INVALID_ARGUMENT, "Sink not found");
            }

            info = it->second;
        }

        if (!info->enabled) {
            return common::Result<>(common::ErrorCode::INVALID_ARGUMENT, "Sink is disabled");
        }

        info->pending_count.fetch_add(batch.size(), std::memory_order_relaxed);

        common::rt::HighResolutionTimer timer;
        auto result  = info->sink->write_batch(batch);
        auto elapsed = timer.elapsed();

        info->pending_count.fetch_sub(batch.size(), std::memory_order_relaxed);

        if (result.is_success()) {
            info->messages_sent.fetch_add(batch.size(), std::memory_order_relaxed);
            info->total_latency_ns.fetch_add(elapsed.count(), std::memory_order_relaxed);
        } else {
            info->messages_failed.fetch_add(batch.size(), std::memory_order_relaxed);
            update_sink_health_on_failure(info);
        }

        return result;
    }

    common::Result<> write_with_load_balancing(const std::vector<std::string>& candidate_ids,
                                               const common::DataPoint& data_point,
                                               LoadBalanceStrategy strategy) {
        auto selection = select_sink(candidate_ids, data_point, strategy);

        if (!selection.success) {
            return common::Result<>(common::ErrorCode::INVALID_ARGUMENT, selection.error_message);
        }

        // For broadcast, write to all selected sinks
        if (strategy == LoadBalanceStrategy::BROADCAST) {
            for (const auto& id : selection.selected_sink_ids) {
                write_to_sink(id, data_point);  // Ignore individual failures
            }
            return common::Result<>();
        }

        // For other strategies, write to single selected sink
        return write_to_sink(selection.selected_sink_ids[0], data_point);
    }

    std::vector<std::pair<std::string, common::Result<>>> write_to_all(
        const std::vector<std::string>& sink_ids, const common::DataPoint& data_point) {
        std::vector<std::pair<std::string, common::Result<>>> results;

        for (const auto& id : sink_ids) {
            results.emplace_back(id, write_to_sink(id, data_point));
        }

        return results;
    }

    SinkHealth get_sink_health(std::string_view id) const {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it == sinks_.end()) {
            return SinkHealth::UNKNOWN;
        }

        return it->second->health;
    }

    SinkHealth check_sink_health(std::string_view id) {
        std::shared_ptr<SinkInfo> info;

        {
            std::shared_lock lock(sinks_mutex_);

            auto it = sinks_.find(std::string(id));
            if (it == sinks_.end()) {
                return SinkHealth::UNKNOWN;
            }

            info = it->second;
        }

        // Check if sink is running and healthy
        if (!info->sink->is_running()) {
            info->health         = SinkHealth::UNHEALTHY;
            info->health_message = "Sink is not running";
        } else if (!info->sink->is_healthy()) {
            info->health         = SinkHealth::DEGRADED;
            info->health_message = info->sink->get_health_status();
        } else {
            info->health = SinkHealth::HEALTHY;
            info->health_message.clear();
        }

        info->last_health_check = common::Timestamp::now();
        update_health_stats();

        return info->health;
    }

    std::vector<std::string> get_healthy_sinks() const {
        std::shared_lock lock(sinks_mutex_);

        std::vector<std::string> healthy;
        for (const auto& [id, info] : sinks_) {
            if (info->enabled && info->health == SinkHealth::HEALTHY) {
                healthy.push_back(id);
            }
        }

        return healthy;
    }

    std::vector<std::string> get_unhealthy_sinks() const {
        std::shared_lock lock(sinks_mutex_);

        std::vector<std::string> unhealthy;
        for (const auto& [id, info] : sinks_) {
            if (info->health == SinkHealth::UNHEALTHY) {
                unhealthy.push_back(id);
            }
        }

        return unhealthy;
    }

    void mark_sink_unhealthy(std::string_view id, std::string_view reason) {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it != sinks_.end()) {
            it->second->health            = SinkHealth::UNHEALTHY;
            it->second->health_message    = std::string(reason);
            it->second->last_health_check = common::Timestamp::now();
        }

        update_health_stats();
    }

    void mark_sink_healthy(std::string_view id) {
        std::shared_lock lock(sinks_mutex_);

        auto it = sinks_.find(std::string(id));
        if (it != sinks_.end()) {
            it->second->health = SinkHealth::HEALTHY;
            it->second->health_message.clear();
            it->second->last_health_check = common::Timestamp::now();
        }

        update_health_stats();
    }

    const SinkRegistryStats& stats() const noexcept { return stats_; }

    void reset_stats() {
        stats_.reset();

        std::shared_lock lock(sinks_mutex_);
        for (auto& [_, info] : sinks_) {
            info->messages_sent.store(0);
            info->messages_failed.store(0);
            info->bytes_sent.store(0);
            info->total_latency_ns.store(0);
        }
    }

    std::unordered_map<std::string, SinkInfo> get_all_sink_stats() const {
        std::shared_lock lock(sinks_mutex_);

        std::unordered_map<std::string, SinkInfo> result;
        for (const auto& [id, info] : sinks_) {
            SinkInfo copy;
            copy.id                = info->id;
            copy.type              = info->type;
            copy.weight            = info->weight;
            copy.enabled           = info->enabled;
            copy.priority          = info->priority;
            copy.health            = info->health;
            copy.last_health_check = info->last_health_check;
            copy.health_message    = info->health_message;

            result[id] = copy;
        }

        return result;
    }

    const SinkRegistryConfig& config() const noexcept { return config_; }

private:
    void health_check_loop() {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(config_.health_check_interval);

            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }

            // Check health of all sinks
            std::vector<std::string> ids;
            {
                std::shared_lock lock(sinks_mutex_);
                for (const auto& [id, _] : sinks_) {
                    ids.push_back(id);
                }
            }

            for (const auto& id : ids) {
                check_sink_health(id);
            }
        }
    }

    void update_sink_health_on_failure(std::shared_ptr<SinkInfo> info) {
        // Track consecutive failures for unhealthy detection
        // This is a simplified version - real impl would track per-sink counters
        if (info->messages_failed.load() > config_.unhealthy_threshold) {
            info->health = SinkHealth::DEGRADED;
        }
    }

    void update_health_stats() {
        std::shared_lock lock(sinks_mutex_);

        uint64_t healthy = 0, degraded = 0, unhealthy = 0;

        for (const auto& [_, info] : sinks_) {
            switch (info->health) {
                case SinkHealth::HEALTHY:
                    ++healthy;
                    break;
                case SinkHealth::DEGRADED:
                    ++degraded;
                    break;
                case SinkHealth::UNHEALTHY:
                    ++unhealthy;
                    break;
                default:
                    break;
            }
        }

        stats_.healthy_sinks.store(healthy, std::memory_order_relaxed);
        stats_.degraded_sinks.store(degraded, std::memory_order_relaxed);
        stats_.unhealthy_sinks.store(unhealthy, std::memory_order_relaxed);
    }

    SinkRegistryConfig config_;
    SinkRegistryStats stats_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    mutable std::shared_mutex sinks_mutex_;
    std::unordered_map<std::string, std::shared_ptr<SinkInfo>> sinks_;

    std::unordered_map<LoadBalanceStrategy, std::unique_ptr<ILoadBalancer>> balancers_;

    std::thread health_check_thread_;
};

// ============================================================================
// SinkRegistry Public Interface
// ============================================================================

SinkRegistry::SinkRegistry() : impl_(std::make_unique<SinkRegistryImpl>(SinkRegistryConfig{})) {}

SinkRegistry::SinkRegistry(const SinkRegistryConfig& config)
    : impl_(std::make_unique<SinkRegistryImpl>(config)) {}

SinkRegistry::~SinkRegistry() = default;

SinkRegistry::SinkRegistry(SinkRegistry&&) noexcept            = default;
SinkRegistry& SinkRegistry::operator=(SinkRegistry&&) noexcept = default;

bool SinkRegistry::start() {
    return impl_->start();
}

void SinkRegistry::stop() {
    impl_->stop();
}

bool SinkRegistry::is_running() const noexcept {
    return impl_->is_running();
}

bool SinkRegistry::register_sink(std::string_view id, std::shared_ptr<common::IIPBSink> sink) {
    return impl_->register_sink(id, std::move(sink), 100);
}

bool SinkRegistry::register_sink(std::string_view id, std::shared_ptr<common::IIPBSink> sink,
                                 uint32_t weight) {
    return impl_->register_sink(id, std::move(sink), weight);
}

bool SinkRegistry::unregister_sink(std::string_view id) {
    return impl_->unregister_sink(id);
}

bool SinkRegistry::has_sink(std::string_view id) const {
    return impl_->has_sink(id);
}

std::shared_ptr<common::IIPBSink> SinkRegistry::get_sink(std::string_view id) {
    return impl_->get_sink(id);
}

std::optional<SinkInfo> SinkRegistry::get_sink_info(std::string_view id) const {
    return impl_->get_sink_info(id);
}

std::vector<std::string> SinkRegistry::get_sink_ids() const {
    return impl_->get_sink_ids();
}

size_t SinkRegistry::sink_count() const noexcept {
    return impl_->sink_count();
}

bool SinkRegistry::set_sink_enabled(std::string_view id, bool enabled) {
    return impl_->set_sink_enabled(id, enabled);
}

bool SinkRegistry::set_sink_weight(std::string_view id, uint32_t weight) {
    return impl_->set_sink_weight(id, weight);
}

bool SinkRegistry::set_sink_priority(std::string_view id, uint32_t priority) {
    return impl_->set_sink_priority(id, priority);
}

SinkSelectionResult SinkRegistry::select_sink(const std::vector<std::string>& candidate_ids,
                                              LoadBalanceStrategy strategy) {
    return impl_->select_sink(candidate_ids, strategy);
}

SinkSelectionResult SinkRegistry::select_sink(const std::vector<std::string>& candidate_ids,
                                              const common::DataPoint& data_point,
                                              LoadBalanceStrategy strategy) {
    return impl_->select_sink(candidate_ids, data_point, strategy);
}

SinkSelectionResult SinkRegistry::select_sink_filtered(
    const std::vector<std::string>& candidate_ids, std::function<bool(const SinkInfo&)> filter,
    LoadBalanceStrategy strategy) {
    return impl_->select_sink_filtered(candidate_ids, std::move(filter), strategy);
}

common::Result<> SinkRegistry::write_to_sink(std::string_view sink_id,
                                             const common::DataPoint& data_point) {
    return impl_->write_to_sink(sink_id, data_point);
}

common::Result<> SinkRegistry::write_batch_to_sink(std::string_view sink_id,
                                                   std::span<const common::DataPoint> batch) {
    return impl_->write_batch_to_sink(sink_id, batch);
}

common::Result<> SinkRegistry::write_with_load_balancing(
    const std::vector<std::string>& candidate_ids, const common::DataPoint& data_point,
    LoadBalanceStrategy strategy) {
    return impl_->write_with_load_balancing(candidate_ids, data_point, strategy);
}

std::vector<std::pair<std::string, common::Result<>>> SinkRegistry::write_to_all(
    const std::vector<std::string>& sink_ids, const common::DataPoint& data_point) {
    return impl_->write_to_all(sink_ids, data_point);
}

SinkHealth SinkRegistry::get_sink_health(std::string_view id) const {
    return impl_->get_sink_health(id);
}

SinkHealth SinkRegistry::check_sink_health(std::string_view id) {
    return impl_->check_sink_health(id);
}

std::vector<std::string> SinkRegistry::get_healthy_sinks() const {
    return impl_->get_healthy_sinks();
}

std::vector<std::string> SinkRegistry::get_unhealthy_sinks() const {
    return impl_->get_unhealthy_sinks();
}

void SinkRegistry::mark_sink_unhealthy(std::string_view id, std::string_view reason) {
    impl_->mark_sink_unhealthy(id, reason);
}

void SinkRegistry::mark_sink_healthy(std::string_view id) {
    impl_->mark_sink_healthy(id);
}

const SinkRegistryStats& SinkRegistry::stats() const noexcept {
    return impl_->stats();
}

void SinkRegistry::reset_stats() {
    impl_->reset_stats();
}

std::unordered_map<std::string, SinkInfo> SinkRegistry::get_all_sink_stats() const {
    return impl_->get_all_sink_stats();
}

const SinkRegistryConfig& SinkRegistry::config() const noexcept {
    return impl_->config();
}

}  // namespace ipb::core
