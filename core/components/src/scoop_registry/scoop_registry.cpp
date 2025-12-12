#include "ipb/core/scoop_registry/scoop_registry.hpp"
#include <ipb/common/endpoint.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/debug.hpp>
#include <ipb/common/platform.hpp>

#include <algorithm>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace ipb::core {

using namespace common::debug;

namespace {
    constexpr std::string_view LOG_CAT = category::PROTOCOL;  // Scoops are protocol sources
} // anonymous namespace

// ============================================================================
// AggregatedSubscription Implementation
// ============================================================================

AggregatedSubscription::~AggregatedSubscription() {
    cancel();
}

AggregatedSubscription::AggregatedSubscription(AggregatedSubscription&& other) noexcept
    : sources_(std::move(other.sources_))
    , registry_(std::move(other.registry_))
    , id_(other.id_) {
    other.id_ = 0;
}

AggregatedSubscription& AggregatedSubscription::operator=(AggregatedSubscription&& other) noexcept {
    if (this != &other) {
        cancel();
        sources_ = std::move(other.sources_);
        registry_ = std::move(other.registry_);
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

bool AggregatedSubscription::is_active() const noexcept {
    return !sources_.empty() && std::any_of(sources_.begin(), sources_.end(),
        [](const auto& s) { return s.active; });
}

void AggregatedSubscription::cancel() {
    for (auto& source : sources_) {
        source.active = false;
    }
    sources_.clear();
}

size_t AggregatedSubscription::source_count() const noexcept {
    return std::count_if(sources_.begin(), sources_.end(),
        [](const auto& s) { return s.active; });
}

// ============================================================================
// ScoopRegistryImpl - Private Implementation
// ============================================================================

class ScoopRegistryImpl : public std::enable_shared_from_this<ScoopRegistryImpl> {
public:
    explicit ScoopRegistryImpl(const ScoopRegistryConfig& config)
        : config_(config) {}

    ~ScoopRegistryImpl() {
        stop();
    }

    bool start() {
        IPB_SPAN_CAT("ScoopRegistry::start", LOG_CAT);

        if (IPB_UNLIKELY(running_.exchange(true))) {
            IPB_LOG_WARN(LOG_CAT, "ScoopRegistry already running");
            return false;
        }

        stop_requested_.store(false);

        if (config_.enable_health_check) {
            IPB_LOG_DEBUG(LOG_CAT, "Starting health check thread");
            health_check_thread_ = std::thread([this]() {
                health_check_loop();
            });
        }

        if (config_.enable_auto_reconnect) {
            IPB_LOG_DEBUG(LOG_CAT, "Starting auto-reconnect thread");
            reconnect_thread_ = std::thread([this]() {
                reconnect_loop();
            });
        }

        IPB_LOG_INFO(LOG_CAT, "ScoopRegistry started");
        return true;
    }

    void stop() {
        IPB_SPAN_CAT("ScoopRegistry::stop", LOG_CAT);

        if (IPB_UNLIKELY(!running_.exchange(false))) {
            IPB_LOG_DEBUG(LOG_CAT, "ScoopRegistry stop called but not running");
            return;
        }

        IPB_LOG_INFO(LOG_CAT, "Stopping ScoopRegistry...");

        stop_requested_.store(true);

        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }

        if (reconnect_thread_.joinable()) {
            reconnect_thread_.join();
        }

        IPB_LOG_INFO(LOG_CAT, "ScoopRegistry stopped");
    }

    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    bool register_scoop(std::string_view id, std::shared_ptr<common::IProtocolSource> scoop,
                       bool is_primary, uint32_t priority) {
        IPB_PRECONDITION(!id.empty());
        IPB_PRECONDITION(scoop != nullptr);

        std::unique_lock lock(scoops_mutex_);

        std::string id_str(id);
        if (IPB_UNLIKELY(scoops_.find(id_str) != scoops_.end())) {
            IPB_LOG_WARN(LOG_CAT, "Scoop already registered: " << id);
            return false;
        }

        auto info = std::make_shared<ScoopInfo>();
        info->id = id_str;
        info->scoop = std::move(scoop);
        info->is_primary = is_primary;
        info->priority = priority;
        info->type = std::string(info->scoop->protocol_name());
        info->health = ScoopHealth::UNKNOWN;

        scoops_[id_str] = std::move(info);
        stats_.active_scoops.fetch_add(1, std::memory_order_relaxed);

        IPB_LOG_INFO(LOG_CAT, "Registered scoop: " << id << " (type=" << info->type
                    << ", primary=" << is_primary << ", priority=" << priority << ")");
        return true;
    }

    bool unregister_scoop(std::string_view id) {
        IPB_PRECONDITION(!id.empty());

        std::unique_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (IPB_UNLIKELY(it == scoops_.end())) {
            IPB_LOG_WARN(LOG_CAT, "Cannot unregister unknown scoop: " << id);
            return false;
        }

        // Disconnect first
        if (it->second->connected) {
            IPB_LOG_DEBUG(LOG_CAT, "Disconnecting scoop before unregister: " << id);
            it->second->scoop->disconnect();
        }

        IPB_LOG_INFO(LOG_CAT, "Unregistering scoop: " << id);
        scoops_.erase(it);
        stats_.active_scoops.fetch_sub(1, std::memory_order_relaxed);

        return true;
    }

    bool has_scoop(std::string_view id) const {
        std::shared_lock lock(scoops_mutex_);
        return scoops_.find(std::string(id)) != scoops_.end();
    }

    std::shared_ptr<common::IProtocolSource> get_scoop(std::string_view id) {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it == scoops_.end()) {
            return nullptr;
        }

        return it->second->scoop;
    }

    std::optional<ScoopInfo> get_scoop_info(std::string_view id) const {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it == scoops_.end()) {
            return std::nullopt;
        }

        ScoopInfo copy;
        copy.id = it->second->id;
        copy.type = it->second->type;
        copy.is_primary = it->second->is_primary;
        copy.priority = it->second->priority;
        copy.enabled = it->second->enabled;
        copy.health = it->second->health;
        copy.connected = it->second->connected;

        return copy;
    }

    std::vector<std::string> get_scoop_ids() const {
        std::shared_lock lock(scoops_mutex_);

        std::vector<std::string> ids;
        ids.reserve(scoops_.size());

        for (const auto& [id, _] : scoops_) {
            ids.push_back(id);
        }

        return ids;
    }

    size_t scoop_count() const noexcept {
        std::shared_lock lock(scoops_mutex_);
        return scoops_.size();
    }

    bool set_scoop_enabled(std::string_view id, bool enabled) {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it == scoops_.end()) {
            return false;
        }

        it->second->enabled = enabled;
        return true;
    }

    bool set_scoop_primary(std::string_view id, bool is_primary) {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it == scoops_.end()) {
            return false;
        }

        it->second->is_primary = is_primary;
        return true;
    }

    bool set_scoop_priority(std::string_view id, uint32_t priority) {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it == scoops_.end()) {
            return false;
        }

        it->second->priority = priority;
        return true;
    }

    ScoopSelectionResult select_scoop(
            const std::vector<std::string>& candidate_ids,
            ReadStrategy strategy) {
        ScoopSelectionResult result;

        std::vector<const ScoopInfo*> candidates;
        {
            std::shared_lock lock(scoops_mutex_);

            for (const auto& id : candidate_ids) {
                auto it = scoops_.find(id);
                if (it != scoops_.end() && it->second->enabled &&
                    it->second->health != ScoopHealth::UNHEALTHY) {
                    candidates.push_back(it->second.get());
                }
            }
        }

        if (candidates.empty()) {
            result.error_message = "No healthy scoops available";
            return result;
        }

        switch (strategy) {
            case ReadStrategy::PRIMARY_ONLY: {
                for (const auto* scoop : candidates) {
                    if (scoop->is_primary) {
                        result.selected_scoop_ids.push_back(scoop->id);
                        break;
                    }
                }
                if (result.selected_scoop_ids.empty() && !candidates.empty()) {
                    result.selected_scoop_ids.push_back(candidates[0]->id);
                }
                break;
            }

            case ReadStrategy::FAILOVER: {
                // Sort by priority (lower = higher priority)
                std::vector<const ScoopInfo*> sorted = candidates;
                std::sort(sorted.begin(), sorted.end(),
                    [](const ScoopInfo* a, const ScoopInfo* b) {
                        if (a->is_primary != b->is_primary) {
                            return a->is_primary;
                        }
                        return a->priority < b->priority;
                    });

                for (const auto* scoop : sorted) {
                    if (scoop->connected && scoop->health == ScoopHealth::HEALTHY) {
                        result.selected_scoop_ids.push_back(scoop->id);
                        break;
                    }
                }

                // If no healthy connected scoop, return first available
                if (result.selected_scoop_ids.empty() && !sorted.empty()) {
                    result.selected_scoop_ids.push_back(sorted[0]->id);
                }
                break;
            }

            case ReadStrategy::ROUND_ROBIN: {
                size_t index = round_robin_counter_.fetch_add(1) % candidates.size();
                result.selected_scoop_ids.push_back(candidates[index]->id);
                break;
            }

            case ReadStrategy::BROADCAST_MERGE:
            case ReadStrategy::QUORUM: {
                for (const auto* scoop : candidates) {
                    result.selected_scoop_ids.push_back(scoop->id);
                }
                break;
            }

            case ReadStrategy::FASTEST_RESPONSE: {
                const ScoopInfo* fastest = nullptr;
                double min_latency = std::numeric_limits<double>::max();

                for (const auto* scoop : candidates) {
                    double latency = scoop->avg_latency_us();
                    if (latency < min_latency || (latency == 0 && fastest == nullptr)) {
                        min_latency = latency;
                        fastest = scoop;
                    }
                }

                if (fastest) {
                    result.selected_scoop_ids.push_back(fastest->id);
                }
                break;
            }
        }

        result.success = !result.selected_scoop_ids.empty();
        return result;
    }

    common::Result<common::DataSet> read_from(
            const std::vector<std::string>& candidate_ids,
            ReadStrategy strategy) {
        auto selection = select_scoop(candidate_ids, strategy);

        if (!selection.success) {
            stats_.failed_reads.fetch_add(1, std::memory_order_relaxed);
            return common::Result<common::DataSet>(
                common::ErrorCode::INVALID_ARGUMENT,
                selection.error_message);
        }

        stats_.total_reads.fetch_add(1, std::memory_order_relaxed);

        if (strategy == ReadStrategy::BROADCAST_MERGE) {
            return read_merged(selection.selected_scoop_ids);
        }

        // Try each selected scoop in order (for failover)
        for (const auto& id : selection.selected_scoop_ids) {
            auto result = read_from_scoop(id);
            if (result.is_success()) {
                return result;
            }

            stats_.failover_events.fetch_add(1, std::memory_order_relaxed);
        }

        stats_.failed_reads.fetch_add(1, std::memory_order_relaxed);
        return common::Result<common::DataSet>(
            common::ErrorCode::UNKNOWN_ERROR,
            "All scoops failed to read");
    }

    common::Result<common::DataSet> read_from_scoop(std::string_view scoop_id) {
        std::shared_ptr<ScoopInfo> info;

        {
            std::shared_lock lock(scoops_mutex_);

            auto it = scoops_.find(std::string(scoop_id));
            if (it == scoops_.end()) {
                return common::Result<common::DataSet>(
                    common::ErrorCode::INVALID_ARGUMENT,
                    "Scoop not found");
            }

            info = it->second;
        }

        if (!info->enabled) {
            return common::Result<common::DataSet>(
                common::ErrorCode::INVALID_ARGUMENT,
                "Scoop is disabled");
        }

        info->reads_attempted.fetch_add(1, std::memory_order_relaxed);

        common::rt::HighResolutionTimer timer;
        auto result = info->scoop->read();
        auto elapsed = timer.elapsed();

        if (result.is_success()) {
            info->reads_successful.fetch_add(1, std::memory_order_relaxed);
            info->data_points_received.fetch_add(
                result.value().size(), std::memory_order_relaxed);
            info->total_latency_ns.fetch_add(elapsed.count(), std::memory_order_relaxed);
            stats_.successful_reads.fetch_add(1, std::memory_order_relaxed);
        } else {
            info->reads_failed.fetch_add(1, std::memory_order_relaxed);
            update_scoop_health_on_failure(info);
        }

        return result;
    }

    common::Result<common::DataSet> read_merged(const std::vector<std::string>& scoop_ids) {
        common::DataSet merged;

        for (const auto& id : scoop_ids) {
            auto result = read_from_scoop(id);
            if (result.is_success()) {
                // Merge data points
                for (const auto& dp : result.value()) {
                    merged.push_back(dp);
                }
            }
        }

        return common::Result<common::DataSet>(std::move(merged));
    }

    AggregatedSubscription subscribe(
            const std::vector<std::string>& scoop_ids,
            AggregatedSubscription::DataCallback data_callback,
            AggregatedSubscription::ErrorCallback error_callback) {
        AggregatedSubscription sub;
        sub.registry_ = shared_from_this();
        sub.id_ = next_subscription_id_.fetch_add(1, std::memory_order_relaxed);

        std::shared_lock lock(scoops_mutex_);

        for (const auto& id : scoop_ids) {
            auto it = scoops_.find(id);
            if (it == scoops_.end() || !it->second->enabled) {
                continue;
            }

            auto& info = it->second;

            // Subscribe to scoop
            auto result = info->scoop->subscribe(
                [data_callback, id](common::DataSet data) {
                    data_callback(data, id);
                },
                [error_callback, id](common::ErrorCode code, std::string_view msg) {
                    if (error_callback) {
                        error_callback(id, code, msg);
                    }
                });

            if (result.is_success()) {
                AggregatedSubscription::SourceSubscription source;
                source.scoop_id = id;
                source.active = true;
                sub.sources_.push_back(std::move(source));
            }
        }

        stats_.active_subscriptions.fetch_add(sub.sources_.size(), std::memory_order_relaxed);

        return sub;
    }

    common::Result<> connect_scoop(std::string_view id) {
        std::shared_ptr<ScoopInfo> info;

        {
            std::shared_lock lock(scoops_mutex_);

            auto it = scoops_.find(std::string(id));
            if (it == scoops_.end()) {
                return common::Result<>(common::ErrorCode::INVALID_ARGUMENT,
                                       "Scoop not found");
            }

            info = it->second;
        }

        auto result = info->scoop->connect();

        if (result.is_success()) {
            info->connected = true;
            info->last_connect_time = common::Timestamp::now();
            info->health = ScoopHealth::HEALTHY;
            stats_.connected_scoops.fetch_add(1, std::memory_order_relaxed);
        }

        return result;
    }

    common::Result<> disconnect_scoop(std::string_view id) {
        std::shared_ptr<ScoopInfo> info;

        {
            std::shared_lock lock(scoops_mutex_);

            auto it = scoops_.find(std::string(id));
            if (it == scoops_.end()) {
                return common::Result<>(common::ErrorCode::INVALID_ARGUMENT,
                                       "Scoop not found");
            }

            info = it->second;
        }

        auto result = info->scoop->disconnect();

        info->connected = false;
        info->last_disconnect_time = common::Timestamp::now();
        info->health = ScoopHealth::DISCONNECTED;
        stats_.connected_scoops.fetch_sub(1, std::memory_order_relaxed);

        return result;
    }

    void connect_all() {
        std::vector<std::string> ids = get_scoop_ids();
        for (const auto& id : ids) {
            connect_scoop(id);
        }
    }

    void disconnect_all() {
        std::vector<std::string> ids = get_scoop_ids();
        for (const auto& id : ids) {
            disconnect_scoop(id);
        }
    }

    std::vector<std::string> get_connected_scoops() const {
        std::shared_lock lock(scoops_mutex_);

        std::vector<std::string> connected;
        for (const auto& [id, info] : scoops_) {
            if (info->connected) {
                connected.push_back(id);
            }
        }

        return connected;
    }

    ScoopHealth get_scoop_health(std::string_view id) const {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it == scoops_.end()) {
            return ScoopHealth::UNKNOWN;
        }

        return it->second->health;
    }

    ScoopHealth check_scoop_health(std::string_view id) {
        std::shared_ptr<ScoopInfo> info;

        {
            std::shared_lock lock(scoops_mutex_);

            auto it = scoops_.find(std::string(id));
            if (it == scoops_.end()) {
                return ScoopHealth::UNKNOWN;
            }

            info = it->second;
        }

        if (!info->scoop->is_running()) {
            info->health = ScoopHealth::UNHEALTHY;
            info->health_message = "Scoop is not running";
        } else if (!info->scoop->is_connected()) {
            info->health = ScoopHealth::DISCONNECTED;
            info->health_message = "Scoop is disconnected";
        } else if (!info->scoop->is_healthy()) {
            info->health = ScoopHealth::DEGRADED;
            info->health_message = info->scoop->get_health_status();
        } else {
            info->health = ScoopHealth::HEALTHY;
            info->health_message.clear();
        }

        info->last_health_check = common::Timestamp::now();
        update_health_stats();

        return info->health;
    }

    std::vector<std::string> get_healthy_scoops() const {
        std::shared_lock lock(scoops_mutex_);

        std::vector<std::string> healthy;
        for (const auto& [id, info] : scoops_) {
            if (info->enabled && info->health == ScoopHealth::HEALTHY) {
                healthy.push_back(id);
            }
        }

        return healthy;
    }

    std::vector<std::string> get_unhealthy_scoops() const {
        std::shared_lock lock(scoops_mutex_);

        std::vector<std::string> unhealthy;
        for (const auto& [id, info] : scoops_) {
            if (info->health == ScoopHealth::UNHEALTHY ||
                info->health == ScoopHealth::DISCONNECTED) {
                unhealthy.push_back(id);
            }
        }

        return unhealthy;
    }

    void mark_scoop_unhealthy(std::string_view id, std::string_view reason) {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it != scoops_.end()) {
            it->second->health = ScoopHealth::UNHEALTHY;
            it->second->health_message = std::string(reason);
            it->second->last_health_check = common::Timestamp::now();
        }

        update_health_stats();
    }

    void mark_scoop_healthy(std::string_view id) {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(id));
        if (it != scoops_.end()) {
            it->second->health = ScoopHealth::HEALTHY;
            it->second->health_message.clear();
            it->second->last_health_check = common::Timestamp::now();
        }

        update_health_stats();
    }

    common::Result<> add_address(const std::vector<std::string>& scoop_ids,
                                 std::string_view address) {
        std::shared_lock lock(scoops_mutex_);

        for (const auto& id : scoop_ids) {
            auto it = scoops_.find(id);
            if (it != scoops_.end()) {
                it->second->scoop->add_address(address);
            }
        }

        return common::Result<>();
    }

    common::Result<> remove_address(const std::vector<std::string>& scoop_ids,
                                    std::string_view address) {
        std::shared_lock lock(scoops_mutex_);

        for (const auto& id : scoop_ids) {
            auto it = scoops_.find(id);
            if (it != scoops_.end()) {
                it->second->scoop->remove_address(address);
            }
        }

        return common::Result<>();
    }

    std::vector<std::string> get_addresses(std::string_view scoop_id) const {
        std::shared_lock lock(scoops_mutex_);

        auto it = scoops_.find(std::string(scoop_id));
        if (it == scoops_.end()) {
            return {};
        }

        return it->second->scoop->get_addresses();
    }

    const ScoopRegistryStats& stats() const noexcept {
        return stats_;
    }

    void reset_stats() {
        stats_.reset();

        std::shared_lock lock(scoops_mutex_);
        for (auto& [_, info] : scoops_) {
            info->reads_attempted.store(0);
            info->reads_successful.store(0);
            info->reads_failed.store(0);
            info->data_points_received.store(0);
            info->bytes_received.store(0);
            info->total_latency_ns.store(0);
        }
    }

    std::unordered_map<std::string, ScoopInfo> get_all_scoop_stats() const {
        std::shared_lock lock(scoops_mutex_);

        std::unordered_map<std::string, ScoopInfo> result;
        for (const auto& [id, info] : scoops_) {
            ScoopInfo copy;
            copy.id = info->id;
            copy.type = info->type;
            copy.is_primary = info->is_primary;
            copy.priority = info->priority;
            copy.enabled = info->enabled;
            copy.health = info->health;
            copy.connected = info->connected;

            result[id] = copy;
        }

        return result;
    }

    const ScoopRegistryConfig& config() const noexcept {
        return config_;
    }

private:
    void health_check_loop() {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(config_.health_check_interval);

            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }

            std::vector<std::string> ids = get_scoop_ids();
            for (const auto& id : ids) {
                check_scoop_health(id);
            }
        }
    }

    void reconnect_loop() {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(config_.reconnect_interval);

            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }

            // Try to reconnect disconnected scoops
            auto unhealthy = get_unhealthy_scoops();
            for (const auto& id : unhealthy) {
                connect_scoop(id);
            }
        }
    }

    void update_scoop_health_on_failure(std::shared_ptr<ScoopInfo> info) {
        auto failed = info->reads_failed.load();
        if (failed > config_.unhealthy_threshold) {
            info->health = ScoopHealth::DEGRADED;
        }
    }

    void update_health_stats() {
        std::shared_lock lock(scoops_mutex_);

        uint64_t healthy = 0, connected = 0, unhealthy = 0;

        for (const auto& [_, info] : scoops_) {
            if (info->health == ScoopHealth::HEALTHY) ++healthy;
            if (info->connected) ++connected;
            if (info->health == ScoopHealth::UNHEALTHY ||
                info->health == ScoopHealth::DISCONNECTED) ++unhealthy;
        }

        stats_.healthy_scoops.store(healthy, std::memory_order_relaxed);
        stats_.connected_scoops.store(connected, std::memory_order_relaxed);
        stats_.unhealthy_scoops.store(unhealthy, std::memory_order_relaxed);
    }

    ScoopRegistryConfig config_;
    ScoopRegistryStats stats_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    mutable std::shared_mutex scoops_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ScoopInfo>> scoops_;

    std::atomic<uint64_t> round_robin_counter_{0};
    std::atomic<uint64_t> next_subscription_id_{1};

    std::thread health_check_thread_;
    std::thread reconnect_thread_;
};

// ============================================================================
// ScoopRegistry Public Interface
// ============================================================================

ScoopRegistry::ScoopRegistry()
    : impl_(std::make_unique<ScoopRegistryImpl>(ScoopRegistryConfig{})) {}

ScoopRegistry::ScoopRegistry(const ScoopRegistryConfig& config)
    : impl_(std::make_unique<ScoopRegistryImpl>(config)) {}

ScoopRegistry::~ScoopRegistry() = default;

ScoopRegistry::ScoopRegistry(ScoopRegistry&&) noexcept = default;
ScoopRegistry& ScoopRegistry::operator=(ScoopRegistry&&) noexcept = default;

bool ScoopRegistry::start() { return impl_->start(); }
void ScoopRegistry::stop() { impl_->stop(); }
bool ScoopRegistry::is_running() const noexcept { return impl_->is_running(); }

bool ScoopRegistry::register_scoop(std::string_view id,
                                   std::shared_ptr<common::IProtocolSource> scoop) {
    return impl_->register_scoop(id, std::move(scoop), false, 0);
}

bool ScoopRegistry::register_scoop(std::string_view id,
                                   std::shared_ptr<common::IProtocolSource> scoop,
                                   bool is_primary) {
    return impl_->register_scoop(id, std::move(scoop), is_primary, 0);
}

bool ScoopRegistry::register_scoop(std::string_view id,
                                   std::shared_ptr<common::IProtocolSource> scoop,
                                   bool is_primary, uint32_t priority) {
    return impl_->register_scoop(id, std::move(scoop), is_primary, priority);
}

bool ScoopRegistry::unregister_scoop(std::string_view id) {
    return impl_->unregister_scoop(id);
}

bool ScoopRegistry::has_scoop(std::string_view id) const {
    return impl_->has_scoop(id);
}

std::shared_ptr<common::IProtocolSource> ScoopRegistry::get_scoop(std::string_view id) {
    return impl_->get_scoop(id);
}

std::optional<ScoopInfo> ScoopRegistry::get_scoop_info(std::string_view id) const {
    return impl_->get_scoop_info(id);
}

std::vector<std::string> ScoopRegistry::get_scoop_ids() const {
    return impl_->get_scoop_ids();
}

size_t ScoopRegistry::scoop_count() const noexcept {
    return impl_->scoop_count();
}

bool ScoopRegistry::set_scoop_enabled(std::string_view id, bool enabled) {
    return impl_->set_scoop_enabled(id, enabled);
}

bool ScoopRegistry::set_scoop_primary(std::string_view id, bool is_primary) {
    return impl_->set_scoop_primary(id, is_primary);
}

bool ScoopRegistry::set_scoop_priority(std::string_view id, uint32_t priority) {
    return impl_->set_scoop_priority(id, priority);
}

ScoopSelectionResult ScoopRegistry::select_scoop(
        const std::vector<std::string>& candidate_ids,
        ReadStrategy strategy) {
    return impl_->select_scoop(candidate_ids, strategy);
}

common::Result<common::DataSet> ScoopRegistry::read_from(
        const std::vector<std::string>& candidate_ids,
        ReadStrategy strategy) {
    return impl_->read_from(candidate_ids, strategy);
}

common::Result<common::DataSet> ScoopRegistry::read_from_scoop(std::string_view scoop_id) {
    return impl_->read_from_scoop(scoop_id);
}

common::Result<common::DataSet> ScoopRegistry::read_merged(
        const std::vector<std::string>& scoop_ids) {
    return impl_->read_merged(scoop_ids);
}

AggregatedSubscription ScoopRegistry::subscribe(
        const std::vector<std::string>& scoop_ids,
        AggregatedSubscription::DataCallback data_callback,
        AggregatedSubscription::ErrorCallback error_callback) {
    return impl_->subscribe(scoop_ids, std::move(data_callback), std::move(error_callback));
}

AggregatedSubscription ScoopRegistry::subscribe_all(
        AggregatedSubscription::DataCallback data_callback,
        AggregatedSubscription::ErrorCallback error_callback) {
    return impl_->subscribe(get_scoop_ids(), std::move(data_callback), std::move(error_callback));
}

common::Result<> ScoopRegistry::connect_scoop(std::string_view id) {
    return impl_->connect_scoop(id);
}

common::Result<> ScoopRegistry::disconnect_scoop(std::string_view id) {
    return impl_->disconnect_scoop(id);
}

void ScoopRegistry::connect_all() { impl_->connect_all(); }
void ScoopRegistry::disconnect_all() { impl_->disconnect_all(); }

std::vector<std::string> ScoopRegistry::get_connected_scoops() const {
    return impl_->get_connected_scoops();
}

ScoopHealth ScoopRegistry::get_scoop_health(std::string_view id) const {
    return impl_->get_scoop_health(id);
}

ScoopHealth ScoopRegistry::check_scoop_health(std::string_view id) {
    return impl_->check_scoop_health(id);
}

std::vector<std::string> ScoopRegistry::get_healthy_scoops() const {
    return impl_->get_healthy_scoops();
}

std::vector<std::string> ScoopRegistry::get_unhealthy_scoops() const {
    return impl_->get_unhealthy_scoops();
}

void ScoopRegistry::mark_scoop_unhealthy(std::string_view id, std::string_view reason) {
    impl_->mark_scoop_unhealthy(id, reason);
}

void ScoopRegistry::mark_scoop_healthy(std::string_view id) {
    impl_->mark_scoop_healthy(id);
}

common::Result<> ScoopRegistry::add_address(const std::vector<std::string>& scoop_ids,
                                            std::string_view address) {
    return impl_->add_address(scoop_ids, address);
}

common::Result<> ScoopRegistry::remove_address(const std::vector<std::string>& scoop_ids,
                                               std::string_view address) {
    return impl_->remove_address(scoop_ids, address);
}

std::vector<std::string> ScoopRegistry::get_addresses(std::string_view scoop_id) const {
    return impl_->get_addresses(scoop_id);
}

const ScoopRegistryStats& ScoopRegistry::stats() const noexcept {
    return impl_->stats();
}

void ScoopRegistry::reset_stats() {
    impl_->reset_stats();
}

std::unordered_map<std::string, ScoopInfo> ScoopRegistry::get_all_scoop_stats() const {
    return impl_->get_all_scoop_stats();
}

const ScoopRegistryConfig& ScoopRegistry::config() const noexcept {
    return impl_->config();
}

} // namespace ipb::core
