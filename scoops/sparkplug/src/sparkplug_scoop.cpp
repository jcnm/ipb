/**
 * @file sparkplug_scoop.cpp
 * @brief Sparkplug B protocol scoop (data collector) implementation
 */

#include "ipb/scoop/sparkplug/sparkplug_scoop.hpp"

#include <ipb/common/debug.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace ipb::scoop::sparkplug {

using namespace common::debug;

namespace {
constexpr std::string_view LOG_CAT = category::PROTOCOL;
}  // anonymous namespace

//=============================================================================
// SparkplugScoopConfig Implementation
//=============================================================================

SparkplugScoopConfig SparkplugScoopConfig::create_default() {
    SparkplugScoopConfig config;
    config.mqtt_config.broker_url = "tcp://localhost:1883";

    // Subscribe to all Sparkplug messages
    SubscriptionFilter filter;
    filter.group_id_pattern  = "+";
    filter.edge_node_pattern = "+";
    filter.device_pattern    = "#";
    config.filters.push_back(filter);

    return config;
}

SparkplugScoopConfig SparkplugScoopConfig::create_high_throughput() {
    SparkplugScoopConfig config = create_default();
    config.message_queue_size   = 100000;
    config.include_metadata     = false;
    return config;
}

SparkplugScoopConfig SparkplugScoopConfig::create_selective(const std::string& group_id) {
    SparkplugScoopConfig config;
    config.mqtt_config.broker_url = "tcp://localhost:1883";

    SubscriptionFilter filter;
    filter.group_id_pattern  = group_id;
    filter.edge_node_pattern = "+";
    filter.device_pattern    = "#";
    config.filters.push_back(filter);

    return config;
}

//=============================================================================
// Node/Device State Tracking
//=============================================================================

struct NodeState {
    std::string group_id;
    std::string edge_node_id;
    bool online              = false;
    uint64_t last_birth_time = 0;
    uint64_t bdseq           = 0;
    std::vector<std::string> metrics;
    std::unordered_map<uint64_t, std::string> alias_to_name;
};

struct DeviceState {
    std::string device_id;
    bool online              = false;
    uint64_t last_birth_time = 0;
    std::vector<std::string> metrics;
    std::unordered_map<uint64_t, std::string> alias_to_name;
};

//=============================================================================
// SparkplugScoop::Impl
//=============================================================================

class SparkplugScoop::Impl {
public:
    explicit Impl(const SparkplugScoopConfig& config)
        : config_(config), running_(false), connected_(false) {
        IPB_LOG_DEBUG(LOG_CAT, "SparkplugScoop::Impl created");
    }

    ~Impl() {
        IPB_LOG_TRACE(LOG_CAT, "SparkplugScoop::Impl destructor");
        stop();
    }

    common::Result<void> start() {
        IPB_SPAN_CAT("SparkplugScoop::start", LOG_CAT);

        if (IPB_UNLIKELY(running_.load())) {
            IPB_LOG_WARN(LOG_CAT, "SparkplugScoop already running");
            return common::Result<void>{};
        }

        IPB_LOG_INFO(LOG_CAT, "Starting SparkplugScoop...");

        // Get or create shared MQTT connection
        auto& manager = transport::mqtt::MQTTConnectionManager::instance();
        connection_   = manager.get_or_create(config_.connection_id, config_.mqtt_config);

        if (IPB_UNLIKELY(!connection_)) {
            IPB_LOG_ERROR(LOG_CAT, "Failed to create MQTT connection");
            return common::Result<void>(common::ErrorCode::CONNECTION_ERROR,
                                        "Failed to create MQTT connection");
        }

        // Setup callbacks
        connection_->set_message_callback(
            [this](const std::string& topic, const std::string& payload, transport::mqtt::QoS /*qos*/,
                   bool retained) { handle_message(topic, payload, retained); });

        connection_->set_connection_callback(
            [this](transport::mqtt::ConnectionState state, const std::string& reason) {
                handle_connection_state(state, reason);
            });

        // Connect
        if (IPB_UNLIKELY(!connection_->connect())) {
            IPB_LOG_ERROR(LOG_CAT, "Failed to connect to MQTT broker");
            return common::Result<void>(common::ErrorCode::CONNECTION_ERROR,
                                        "Failed to connect to MQTT broker");
        }

        running_.store(true);

        // Start processing thread
        processing_thread_ = std::thread(&Impl::processing_loop, this);

        // Subscribe to Sparkplug topics
        subscribe_all();

        IPB_LOG_INFO(LOG_CAT, "SparkplugScoop started successfully");
        return common::Result<void>{};
    }

    common::Result<void> stop() {
        IPB_SPAN_CAT("SparkplugScoop::stop", LOG_CAT);

        if (!running_.load()) {
            IPB_LOG_DEBUG(LOG_CAT, "SparkplugScoop already stopped");
            return common::Result<void>{};
        }

        IPB_LOG_INFO(LOG_CAT, "Stopping SparkplugScoop...");

        running_.store(false);

        // Notify processing thread
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();
        }

        // Wait for processing thread
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }

        // Unsubscribe
        if (connection_) {
            for (const auto& filter : config_.filters) {
                for (const auto& topic : filter.to_mqtt_topics()) {
                    connection_->unsubscribe(topic);
                }
            }
        }

        connected_.store(false);

        IPB_LOG_INFO(LOG_CAT, "SparkplugScoop stopped successfully");
        return common::Result<void>{};
    }

    bool is_running() const noexcept { return running_.load(); }

    bool is_connected() const noexcept {
        return connected_.load() && connection_ && connection_->is_connected();
    }

    common::Result<common::DataSet> read() {
        std::lock_guard<std::mutex> lock(buffer_mutex_);

        common::DataSet result;
        while (!data_buffer_.empty()) {
            result.push_back(std::move(data_buffer_.front()));
            data_buffer_.pop();
        }

        return common::Result<common::DataSet>(std::move(result));
    }

    common::Result<void> subscribe(DataCallback data_cb, ErrorCallback error_cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        data_callback_  = std::move(data_cb);
        error_callback_ = std::move(error_cb);
        return common::Result<void>{};
    }

    common::Result<void> unsubscribe() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        data_callback_  = nullptr;
        error_callback_ = nullptr;
        return common::Result<void>{};
    }

    common::Result<void> add_address(std::string_view address) {
        // For Sparkplug, address is a topic pattern
        SubscriptionFilter filter;
        filter.group_id_pattern  = std::string(address);
        filter.edge_node_pattern = "+";
        filter.device_pattern    = "#";

        {
            std::lock_guard<std::mutex> lock(filters_mutex_);
            config_.filters.push_back(filter);
        }

        if (connection_ && connection_->is_connected()) {
            for (const auto& topic : filter.to_mqtt_topics()) {
                connection_->subscribe(topic, transport::mqtt::QoS::AT_LEAST_ONCE);
            }
        }

        return common::Result<void>{};
    }

    common::Result<void> remove_address(std::string_view address) {
        std::string addr(address);

        std::lock_guard<std::mutex> lock(filters_mutex_);
        auto& filters = config_.filters;
        filters.erase(
            std::remove_if(filters.begin(), filters.end(),
                           [&](const SubscriptionFilter& f) { return f.group_id_pattern == addr; }),
            filters.end());

        return common::Result<void>{};
    }

    std::vector<std::string> get_addresses() const {
        std::lock_guard<std::mutex> lock(filters_mutex_);
        std::vector<std::string> addresses;
        for (const auto& filter : config_.filters) {
            addresses.push_back(filter.group_id_pattern);
        }
        return addresses;
    }

    std::vector<std::string> get_online_nodes() const {
        std::shared_lock lock(state_mutex_);
        std::vector<std::string> nodes;
        for (const auto& [key, state] : node_states_) {
            if (state.online) {
                nodes.push_back(state.edge_node_id);
            }
        }
        return nodes;
    }

    std::vector<std::string> get_online_devices(const std::string& edge_node_id) const {
        std::shared_lock lock(state_mutex_);
        std::vector<std::string> devices;

        auto it = node_states_.find(edge_node_id);
        if (it == node_states_.end()) {
            return devices;
        }

        auto dev_it = device_states_.find(edge_node_id);
        if (dev_it != device_states_.end()) {
            for (const auto& [device_id, state] : dev_it->second) {
                if (state.online) {
                    devices.push_back(device_id);
                }
            }
        }

        return devices;
    }

    bool is_node_online(const std::string& edge_node_id) const {
        std::shared_lock lock(state_mutex_);
        auto it = node_states_.find(edge_node_id);
        return it != node_states_.end() && it->second.online;
    }

    bool is_device_online(const std::string& edge_node_id, const std::string& device_id) const {
        std::shared_lock lock(state_mutex_);

        auto node_it = device_states_.find(edge_node_id);
        if (node_it == device_states_.end()) {
            return false;
        }

        auto dev_it = node_it->second.find(device_id);
        return dev_it != node_it->second.end() && dev_it->second.online;
    }

    std::vector<std::string> get_node_metrics(const std::string& edge_node_id) const {
        std::shared_lock lock(state_mutex_);
        auto it = node_states_.find(edge_node_id);
        if (it != node_states_.end()) {
            return it->second.metrics;
        }
        return {};
    }

    std::vector<std::string> get_device_metrics(const std::string& edge_node_id,
                                                const std::string& device_id) const {
        std::shared_lock lock(state_mutex_);

        auto node_it = device_states_.find(edge_node_id);
        if (node_it == device_states_.end()) {
            return {};
        }

        auto dev_it = node_it->second.find(device_id);
        if (dev_it != node_it->second.end()) {
            return dev_it->second.metrics;
        }

        return {};
    }

    bool is_healthy() const noexcept { return running_.load() && is_connected(); }

    void reset_statistics() {
        stats_.messages_received.store(0);
        stats_.messages_processed.store(0);
        stats_.messages_dropped.store(0);
        stats_.births_received.store(0);
        stats_.deaths_received.store(0);
        stats_.data_messages_received.store(0);
        stats_.decode_errors.store(0);
    }

    struct Statistics {
        std::atomic<uint64_t> messages_received{0};
        std::atomic<uint64_t> messages_processed{0};
        std::atomic<uint64_t> messages_dropped{0};
        std::atomic<uint64_t> births_received{0};
        std::atomic<uint64_t> deaths_received{0};
        std::atomic<uint64_t> data_messages_received{0};
        std::atomic<uint64_t> decode_errors{0};
    } stats_;

private:
    void subscribe_all() {
        std::lock_guard<std::mutex> lock(filters_mutex_);

        for (const auto& filter : config_.filters) {
            for (const auto& topic : filter.to_mqtt_topics()) {
                connection_->subscribe(topic, transport::mqtt::QoS::AT_LEAST_ONCE);
                IPB_LOG_DEBUG(LOG_CAT, "Subscribed to: " << topic);
            }
        }
    }

    void handle_connection_state(transport::mqtt::ConnectionState state,
                                 const std::string& reason) {
        if (state == transport::mqtt::ConnectionState::CONNECTED) {
            connected_.store(true);
            IPB_LOG_INFO(LOG_CAT, "Connected to MQTT broker");
            subscribe_all();
        } else if (state == transport::mqtt::ConnectionState::DISCONNECTED ||
                   state == transport::mqtt::ConnectionState::FAILED) {
            connected_.store(false);
            IPB_LOG_WARN(LOG_CAT, "Disconnected from MQTT broker: " << reason);
        }
    }

    void handle_message(const std::string& topic, const std::string& payload, bool retained) {
        stats_.messages_received++;

        // Parse topic
        auto parsed_topic = SparkplugTopic::parse(topic);
        if (!parsed_topic) {
            IPB_LOG_TRACE(LOG_CAT, "Ignoring non-Sparkplug topic: " << topic);
            return;
        }

        IPB_LOG_TRACE(LOG_CAT, "Received " << message_type_to_string(parsed_topic->message_type)
                                           << " from " << parsed_topic->edge_node_id);

        // Handle based on message type
        switch (parsed_topic->message_type) {
            case MessageType::NBIRTH:
                if (config_.process_births) {
                    handle_nbirth(*parsed_topic, payload);
                }
                break;

            case MessageType::NDEATH:
                if (config_.process_deaths) {
                    handle_ndeath(*parsed_topic, payload);
                }
                break;

            case MessageType::NDATA:
                if (config_.process_data) {
                    handle_ndata(*parsed_topic, payload);
                }
                break;

            case MessageType::DBIRTH:
                if (config_.process_births) {
                    handle_dbirth(*parsed_topic, payload);
                }
                break;

            case MessageType::DDEATH:
                if (config_.process_deaths) {
                    handle_ddeath(*parsed_topic, payload);
                }
                break;

            case MessageType::DDATA:
                if (config_.process_data) {
                    handle_ddata(*parsed_topic, payload);
                }
                break;

            case MessageType::STATE:
                handle_state(*parsed_topic, payload);
                break;

            default:
                // Ignore commands and unknown types
                break;
        }
    }

    void handle_nbirth(const SparkplugTopic& topic, const std::string& payload) {
        IPB_LOG_INFO(LOG_CAT, "Node birth: " << topic.edge_node_id);
        stats_.births_received++;

        // Decode payload
        std::vector<uint8_t> data(payload.begin(), payload.end());
        auto decoded = SparkplugPayload::decode(data);

        if (!decoded) {
            stats_.decode_errors++;
            return;
        }

        // Update node state
        {
            std::unique_lock lock(state_mutex_);

            auto& state           = node_states_[topic.edge_node_id];
            state.group_id        = topic.group_id;
            state.edge_node_id    = topic.edge_node_id;
            state.online          = true;
            state.last_birth_time = decoded->timestamp;
            state.metrics.clear();
            state.alias_to_name.clear();

            // Store metric names and aliases
            for (const auto& metric : decoded->metrics) {
                state.metrics.push_back(metric.name);
                if (metric.alias > 0) {
                    state.alias_to_name[metric.alias] = metric.name;
                }
            }
        }

        // Convert metrics to DataPoints
        process_metrics(*decoded, topic.edge_node_id, "");
    }

    void handle_ndeath(const SparkplugTopic& topic, const std::string& payload) {
        IPB_LOG_INFO(LOG_CAT, "Node death: " << topic.edge_node_id);
        stats_.deaths_received++;

        std::unique_lock lock(state_mutex_);

        auto it = node_states_.find(topic.edge_node_id);
        if (it != node_states_.end()) {
            it->second.online = false;
        }

        // Mark all devices as offline too
        auto dev_it = device_states_.find(topic.edge_node_id);
        if (dev_it != device_states_.end()) {
            for (auto& [_, state] : dev_it->second) {
                state.online = false;
            }
        }
    }

    void handle_ndata(const SparkplugTopic& topic, const std::string& payload) {
        stats_.data_messages_received++;

        std::vector<uint8_t> data(payload.begin(), payload.end());
        auto decoded = SparkplugPayload::decode(data);

        if (!decoded) {
            stats_.decode_errors++;
            return;
        }

        // Resolve aliases if needed
        resolve_aliases(topic.edge_node_id, "", decoded->metrics);

        // Convert to DataPoints
        process_metrics(*decoded, topic.edge_node_id, "");
    }

    void handle_dbirth(const SparkplugTopic& topic, const std::string& payload) {
        IPB_LOG_INFO(LOG_CAT,
                     "Device birth: " << topic.device_id << " on node " << topic.edge_node_id);
        stats_.births_received++;

        std::vector<uint8_t> data(payload.begin(), payload.end());
        auto decoded = SparkplugPayload::decode(data);

        if (!decoded) {
            stats_.decode_errors++;
            return;
        }

        // Update device state
        {
            std::unique_lock lock(state_mutex_);

            auto& state           = device_states_[topic.edge_node_id][topic.device_id];
            state.device_id       = topic.device_id;
            state.online          = true;
            state.last_birth_time = decoded->timestamp;
            state.metrics.clear();
            state.alias_to_name.clear();

            for (const auto& metric : decoded->metrics) {
                state.metrics.push_back(metric.name);
                if (metric.alias > 0) {
                    state.alias_to_name[metric.alias] = metric.name;
                }
            }
        }

        process_metrics(*decoded, topic.edge_node_id, topic.device_id);
    }

    void handle_ddeath(const SparkplugTopic& topic, const std::string& payload) {
        IPB_LOG_INFO(LOG_CAT,
                     "Device death: " << topic.device_id << " on node " << topic.edge_node_id);
        stats_.deaths_received++;

        std::unique_lock lock(state_mutex_);

        auto node_it = device_states_.find(topic.edge_node_id);
        if (node_it != device_states_.end()) {
            auto dev_it = node_it->second.find(topic.device_id);
            if (dev_it != node_it->second.end()) {
                dev_it->second.online = false;
            }
        }
    }

    void handle_ddata(const SparkplugTopic& topic, const std::string& payload) {
        stats_.data_messages_received++;

        std::vector<uint8_t> data(payload.begin(), payload.end());
        auto decoded = SparkplugPayload::decode(data);

        if (!decoded) {
            stats_.decode_errors++;
            return;
        }

        resolve_aliases(topic.edge_node_id, topic.device_id, decoded->metrics);
        process_metrics(*decoded, topic.edge_node_id, topic.device_id);
    }

    void handle_state(const SparkplugTopic& topic, const std::string& payload) {
        IPB_LOG_DEBUG(LOG_CAT, "Host state: " << topic.edge_node_id << " = " << payload);
        // Host application state - could trigger actions like rebirth
    }

    void resolve_aliases(const std::string& edge_node_id, const std::string& device_id,
                         std::vector<SparkplugMetric>& metrics) {
        std::shared_lock lock(state_mutex_);

        const std::unordered_map<uint64_t, std::string>* alias_map = nullptr;

        if (device_id.empty()) {
            auto it = node_states_.find(edge_node_id);
            if (it != node_states_.end()) {
                alias_map = &it->second.alias_to_name;
            }
        } else {
            auto node_it = device_states_.find(edge_node_id);
            if (node_it != device_states_.end()) {
                auto dev_it = node_it->second.find(device_id);
                if (dev_it != node_it->second.end()) {
                    alias_map = &dev_it->second.alias_to_name;
                }
            }
        }

        if (!alias_map)
            return;

        for (auto& metric : metrics) {
            if (metric.name.empty() && metric.alias > 0) {
                auto it = alias_map->find(metric.alias);
                if (it != alias_map->end()) {
                    metric.name = it->second;
                }
            }
        }
    }

    void process_metrics(const SparkplugPayload& payload, const std::string& edge_node_id,
                         const std::string& device_id) {
        std::vector<common::DataPoint> data_points;
        data_points.reserve(payload.metrics.size());

        for (const auto& metric : payload.metrics) {
            if (metric.name.empty())
                continue;  // Skip unresolved aliases

            auto dp = metric.to_data_point(edge_node_id, device_id);
            data_points.push_back(std::move(dp));
        }

        if (data_points.empty())
            return;

        stats_.messages_processed++;

        // Buffer or deliver
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            for (auto& dp : data_points) {
                if (data_buffer_.size() < config_.message_queue_size) {
                    data_buffer_.push(std::move(dp));
                } else {
                    stats_.messages_dropped++;
                }
            }
            buffer_cv_.notify_one();
        }
    }

    void processing_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lock(buffer_mutex_);

            buffer_cv_.wait_for(lock, std::chrono::milliseconds{100},
                                [this] { return !data_buffer_.empty() || !running_.load(); });

            if (!running_.load())
                break;

            // Deliver buffered data to callback
            std::vector<common::DataPoint> batch;
            while (!data_buffer_.empty() && batch.size() < 100) {
                batch.push_back(std::move(data_buffer_.front()));
                data_buffer_.pop();
            }

            lock.unlock();

            if (!batch.empty()) {
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (data_callback_) {
                    common::DataSet ds;
                    for (auto& dp : batch) {
                        ds.push_back(std::move(dp));
                    }
                    data_callback_(std::move(ds));
                }
            }
        }
    }

    SparkplugScoopConfig config_;
    std::shared_ptr<transport::mqtt::MQTTConnection> connection_;

    std::atomic<bool> running_;
    std::atomic<bool> connected_;

    // Data buffer
    std::queue<common::DataPoint> data_buffer_;
    mutable std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;

    // Callbacks
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    mutable std::mutex callback_mutex_;

    // Filters
    mutable std::mutex filters_mutex_;

    // State tracking
    mutable std::shared_mutex state_mutex_;
    std::unordered_map<std::string, NodeState> node_states_;
    std::unordered_map<std::string, std::unordered_map<std::string, DeviceState>> device_states_;

    // Processing thread
    std::thread processing_thread_;
};

//=============================================================================
// SparkplugScoop Implementation
//=============================================================================

SparkplugScoop::SparkplugScoop(const SparkplugScoopConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

SparkplugScoop::~SparkplugScoop() = default;

common::Result<common::DataSet> SparkplugScoop::read() {
    return impl_->read();
}

common::Result<common::DataSet> SparkplugScoop::read_async() {
    return impl_->read();
}

common::Result<void> SparkplugScoop::subscribe(DataCallback data_cb, ErrorCallback error_cb) {
    return impl_->subscribe(std::move(data_cb), std::move(error_cb));
}

common::Result<void> SparkplugScoop::unsubscribe() {
    return impl_->unsubscribe();
}

common::Result<void> SparkplugScoop::add_address(std::string_view address) {
    return impl_->add_address(address);
}

common::Result<void> SparkplugScoop::remove_address(std::string_view address) {
    return impl_->remove_address(address);
}

std::vector<std::string> SparkplugScoop::get_addresses() const {
    return impl_->get_addresses();
}

common::Result<void> SparkplugScoop::connect() {
    return impl_->start();
}

common::Result<void> SparkplugScoop::disconnect() {
    return impl_->stop();
}

bool SparkplugScoop::is_connected() const noexcept {
    return impl_->is_connected();
}

common::Result<void> SparkplugScoop::start() {
    return impl_->start();
}

common::Result<void> SparkplugScoop::stop() {
    return impl_->stop();
}

bool SparkplugScoop::is_running() const noexcept {
    return impl_->is_running();
}

common::Result<void> SparkplugScoop::configure(const common::ConfigurationBase& /*config*/) {
    return common::Result<void>{};
}

std::unique_ptr<common::ConfigurationBase> SparkplugScoop::get_configuration() const {
    return nullptr;
}

common::Statistics SparkplugScoop::get_statistics() const noexcept {
    common::Statistics stats;
    stats.total_messages      = impl_->stats_.messages_received.load();
    stats.successful_messages = impl_->stats_.messages_processed.load();
    stats.failed_messages     = impl_->stats_.decode_errors.load();
    return stats;
}

void SparkplugScoop::reset_statistics() noexcept {
    impl_->reset_statistics();
}

bool SparkplugScoop::is_healthy() const noexcept {
    return impl_->is_healthy();
}

std::string SparkplugScoop::get_health_status() const {
    if (impl_->is_healthy()) {
        return "healthy";
    } else if (!impl_->is_running()) {
        return "stopped";
    } else if (!impl_->is_connected()) {
        return "disconnected";
    } else {
        return "unhealthy";
    }
}

std::vector<std::string> SparkplugScoop::get_online_nodes() const {
    return impl_->get_online_nodes();
}

std::vector<std::string> SparkplugScoop::get_online_devices(const std::string& edge_node_id) const {
    return impl_->get_online_devices(edge_node_id);
}

bool SparkplugScoop::is_node_online(const std::string& edge_node_id) const {
    return impl_->is_node_online(edge_node_id);
}

bool SparkplugScoop::is_device_online(const std::string& edge_node_id,
                                      const std::string& device_id) const {
    return impl_->is_device_online(edge_node_id, device_id);
}

std::vector<std::string> SparkplugScoop::get_node_metrics(const std::string& edge_node_id) const {
    return impl_->get_node_metrics(edge_node_id);
}

std::vector<std::string> SparkplugScoop::get_device_metrics(const std::string& edge_node_id,
                                                            const std::string& device_id) const {
    return impl_->get_device_metrics(edge_node_id, device_id);
}

//=============================================================================
// SparkplugScoopFactory Implementation
//=============================================================================

std::unique_ptr<SparkplugScoop> SparkplugScoopFactory::create(const std::string& broker_url) {
    auto config                   = SparkplugScoopConfig::create_default();
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<SparkplugScoop>(config);
}

std::unique_ptr<SparkplugScoop> SparkplugScoopFactory::create_for_group(
    const std::string& broker_url, const std::string& group_id) {
    auto config                   = SparkplugScoopConfig::create_selective(group_id);
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<SparkplugScoop>(config);
}

std::unique_ptr<SparkplugScoop> SparkplugScoopFactory::create(const SparkplugScoopConfig& config) {
    return std::make_unique<SparkplugScoop>(config);
}

std::unique_ptr<SparkplugScoop> SparkplugScoopFactory::create_high_throughput(
    const std::string& broker_url) {
    auto config                   = SparkplugScoopConfig::create_high_throughput();
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<SparkplugScoop>(config);
}

}  // namespace ipb::scoop::sparkplug
