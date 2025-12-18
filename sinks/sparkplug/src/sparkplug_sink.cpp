/**
 * @file sparkplug_sink.cpp
 * @brief Sparkplug B protocol sink implementation
 */

#include "ipb/sink/sparkplug/sparkplug_sink.hpp"

#include <ipb/common/debug.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <algorithm>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <span>
#include <thread>
#include <unordered_map>

namespace ipb::sink::sparkplug {

using namespace common::debug;

namespace {
constexpr std::string_view LOG_CAT = category::PROTOCOL;
}  // anonymous namespace

//=============================================================================
// MetricDefinition Implementation
//=============================================================================

MetricDefinition MetricDefinition::from_data_point(const common::DataPoint& dp) {
    MetricDefinition def;
    def.name = std::string(dp.address());

    // Infer datatype from value
    const auto& value = dp.value();
    switch (value.type()) {
        case common::Value::Type::BOOL:
            def.datatype = types::DataType::Boolean;
            break;
        case common::Value::Type::INT8:
            def.datatype = types::DataType::Int8;
            break;
        case common::Value::Type::INT16:
            def.datatype = types::DataType::Int16;
            break;
        case common::Value::Type::INT32:
            def.datatype = types::DataType::Int32;
            break;
        case common::Value::Type::INT64:
            def.datatype = types::DataType::Int64;
            break;
        case common::Value::Type::UINT8:
            def.datatype = types::DataType::UInt8;
            break;
        case common::Value::Type::UINT16:
            def.datatype = types::DataType::UInt16;
            break;
        case common::Value::Type::UINT32:
            def.datatype = types::DataType::UInt32;
            break;
        case common::Value::Type::UINT64:
            def.datatype = types::DataType::UInt64;
            break;
        case common::Value::Type::FLOAT32:
            def.datatype = types::DataType::Float;
            break;
        case common::Value::Type::FLOAT64:
            def.datatype = types::DataType::Double;
            break;
        case common::Value::Type::STRING:
            def.datatype = types::DataType::String;
            break;
        default:
            def.datatype = types::DataType::Unknown;
            break;
    }

    return def;
}

//=============================================================================
// SparkplugSinkConfig Implementation
//=============================================================================

bool SparkplugSinkConfig::is_valid() const {
    if (edge_node.group_id.empty())
        return false;
    if (edge_node.edge_node_id.empty())
        return false;
    if (!mqtt_config.is_valid())
        return false;
    return true;
}

std::string SparkplugSinkConfig::validation_error() const {
    if (edge_node.group_id.empty())
        return "Group ID is required";
    if (edge_node.edge_node_id.empty())
        return "Edge Node ID is required";
    if (!mqtt_config.is_valid())
        return mqtt_config.validation_error();
    return "";
}

SparkplugSinkConfig SparkplugSinkConfig::create_default(const std::string& group_id,
                                                        const std::string& edge_node_id) {
    SparkplugSinkConfig config;
    config.edge_node.group_id     = group_id;
    config.edge_node.edge_node_id = edge_node_id;
    config.mqtt_config.broker_url = "tcp://localhost:1883";
    return config;
}

SparkplugSinkConfig SparkplugSinkConfig::create_high_throughput(const std::string& group_id,
                                                                const std::string& edge_node_id) {
    SparkplugSinkConfig config        = create_default(group_id, edge_node_id);
    config.publishing.enable_batching = true;
    config.publishing.batch_size      = 500;
    config.publishing.batch_timeout   = std::chrono::milliseconds{500};
    config.publishing.data_qos        = transport::mqtt::QoS::AT_MOST_ONCE;
    config.message_queue_size         = 50000;
    config.worker_threads             = 4;
    return config;
}

SparkplugSinkConfig SparkplugSinkConfig::create_reliable(const std::string& group_id,
                                                         const std::string& edge_node_id) {
    SparkplugSinkConfig config        = create_default(group_id, edge_node_id);
    config.publishing.enable_batching = true;
    config.publishing.batch_size      = 50;
    config.publishing.data_qos        = transport::mqtt::QoS::AT_LEAST_ONCE;
    config.publishing.birth_qos       = transport::mqtt::QoS::AT_LEAST_ONCE;
    config.publishing.death_qos       = transport::mqtt::QoS::AT_LEAST_ONCE;
    return config;
}

//=============================================================================
// SparkplugSink::Impl
//=============================================================================

class SparkplugSink::Impl {
public:
    explicit Impl(const SparkplugSinkConfig& config)
        : config_(config), running_(false), connected_(false), sequence_number_(0), bdseq_(0) {
        IPB_LOG_DEBUG(LOG_CAT, "SparkplugSink::Impl created for group="
                                   << config_.edge_node.group_id
                                   << " node=" << config_.edge_node.edge_node_id);
    }

    ~Impl() {
        IPB_LOG_TRACE(LOG_CAT, "SparkplugSink::Impl destructor");
        stop();
    }

    common::Result<void> start() {
        IPB_SPAN_CAT("SparkplugSink::start", LOG_CAT);

        if (IPB_UNLIKELY(running_.load())) {
            IPB_LOG_WARN(LOG_CAT, "SparkplugSink already running");
            return common::Result<void>{};
        }

        IPB_LOG_INFO(LOG_CAT, "Starting SparkplugSink...");

        // Validate configuration
        if (!config_.is_valid()) {
            IPB_LOG_ERROR(LOG_CAT, "Invalid configuration: " << config_.validation_error());
            return common::Result<void>(common::ErrorCode::INVALID_ARGUMENT,
                                        "Invalid configuration: " + config_.validation_error());
        }

        // Setup Last Will Testament (NDEATH) via config before connection
        setup_death_certificate();

        // Get or create shared MQTT connection
        auto& manager = transport::mqtt::MQTTConnectionManager::instance();
        connection_   = manager.get_or_create(config_.connection_id, config_.mqtt_config);

        if (IPB_UNLIKELY(!connection_)) {
            IPB_LOG_ERROR(LOG_CAT, "Failed to create MQTT connection");
            return common::Result<void>(common::ErrorCode::CONNECTION_ERROR,
                                        "Failed to create MQTT connection");
        }

        // Setup callbacks
        connection_->set_connection_callback(
            [this](transport::mqtt::ConnectionState state, const std::string& reason) {
                handle_connection_state(state, reason);
            });

        connection_->set_message_callback(
            [this](const std::string& topic, const std::string& payload, transport::mqtt::QoS /*qos*/,
                   bool /*retained*/) { handle_message(topic, payload); });

        // Connect
        if (IPB_UNLIKELY(!connection_->connect())) {
            IPB_LOG_ERROR(LOG_CAT, "Failed to connect to MQTT broker");
            return common::Result<void>(common::ErrorCode::CONNECTION_ERROR,
                                        "Failed to connect to MQTT broker");
        }

        running_.store(true);

        // Start worker threads
        for (size_t i = 0; i < config_.worker_threads; ++i) {
            worker_threads_.emplace_back(&Impl::worker_loop, this);
        }

        // Start batch thread if batching is enabled
        if (config_.publishing.enable_batching) {
            batch_thread_ = std::thread(&Impl::batch_loop, this);
        }

        // Subscribe to NCMD topic for rebirth requests
        if (config_.host.auto_rebirth_on_request) {
            subscribe_to_commands();
        }

        // Publish birth certificate
        publish_birth();

        IPB_LOG_INFO(LOG_CAT, "SparkplugSink started successfully");
        return common::Result<void>{};
    }

    common::Result<void> stop() {
        IPB_SPAN_CAT("SparkplugSink::stop", LOG_CAT);

        if (!running_.load()) {
            IPB_LOG_DEBUG(LOG_CAT, "SparkplugSink already stopped");
            return common::Result<void>{};
        }

        IPB_LOG_INFO(LOG_CAT, "Stopping SparkplugSink...");

        running_.store(false);

        // Notify threads
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_cv_.notify_all();
        }

        // Wait for worker threads
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads_.clear();

        // Wait for batch thread
        if (batch_thread_.joinable()) {
            batch_thread_.join();
        }

        // Flush remaining messages
        flush_batch();

        // Note: NDEATH is published via LWT when connection closes
        connected_.store(false);

        IPB_LOG_INFO(LOG_CAT, "SparkplugSink stopped successfully");
        return common::Result<void>{};
    }

    bool is_running() const noexcept { return running_.load(); }

    bool is_connected() const noexcept {
        return connected_.load() && connection_ && connection_->is_connected();
    }

    common::Result<void> write(const common::DataPoint& data_point) {
        if (IPB_UNLIKELY(!running_.load())) {
            return common::Result<void>(common::ErrorCode::INVALID_STATE,
                                        "SparkplugSink is not running");
        }

        // Add to queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (message_queue_.size() >= config_.message_queue_size) {
                stats_.publish_failures++;
                return common::Result<void>(common::ErrorCode::BUFFER_FULL,
                                            "Message queue is full");
            }
            message_queue_.push(data_point);
        }
        queue_cv_.notify_one();

        return common::Result<void>{};
    }

    common::Result<void> write_batch(std::span<const common::DataPoint> data_points) {
        for (const auto& dp : data_points) {
            auto result = write(dp);
            if (!result.is_success()) {
                return result;
            }
        }
        return common::Result<void>{};
    }

    common::Result<void> write_dataset(const common::DataSet& data_set) {
        for (const auto& dp : data_set) {
            auto result = write(dp);
            if (!result.is_success()) {
                return result;
            }
        }
        return common::Result<void>{};
    }

    std::future<common::Result<void>> write_async(const common::DataPoint& data_point) {
        return std::async(std::launch::async, [this, data_point]() {
            return write(data_point);
        });
    }

    std::future<common::Result<void>> write_batch_async(std::span<const common::DataPoint> data_points) {
        std::vector<common::DataPoint> data_copy(data_points.begin(), data_points.end());
        return std::async(std::launch::async, [this, data_copy = std::move(data_copy)]() {
            return write_batch(data_copy);
        });
    }

    common::Result<void> flush() {
        flush_batch();
        return common::Result<void>{};
    }

    size_t pending_count() const noexcept {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return message_queue_.size();
    }

    bool can_accept_data() const noexcept {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return running_.load() && message_queue_.size() < config_.message_queue_size;
    }

    size_t max_batch_size() const noexcept {
        return config_.publishing.batch_size;
    }

    common::Result<void> rebirth() {
        IPB_LOG_INFO(LOG_CAT, "Rebirth requested");
        bdseq_++;
        sequence_number_.store(0);
        return publish_birth();
    }

    void add_node_metric(const MetricDefinition& metric) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        node_metrics_[metric.name] = metric;

        // Assign alias if not set
        if (metric.alias == 0) {
            node_metrics_[metric.name].alias = next_alias_++;
        }
    }

    common::Result<void> add_device(const DeviceConfig& device) {
        {
            std::lock_guard<std::mutex> lock(devices_mutex_);
            devices_[device.device_id] = device;
        }

        // Publish DBIRTH
        return publish_device_birth(device.device_id);
    }

    common::Result<void> remove_device(const std::string& device_id) {
        // Publish DDEATH first
        publish_device_death(device_id);

        {
            std::lock_guard<std::mutex> lock(devices_mutex_);
            devices_.erase(device_id);
        }

        return common::Result<void>{};
    }

    uint64_t get_sequence_number() const noexcept { return sequence_number_.load(); }

    uint64_t get_bdseq() const noexcept { return bdseq_.load(); }

    SparkplugSinkStatistics get_sparkplug_statistics() const {
        return SparkplugSinkStatistics::from_internal(stats_);
    }

    bool is_healthy() const noexcept {
        if (!running_.load() || !is_connected())
            return false;
        return stats_.publish_failures.load() < 100;  // Threshold
    }

    void reset_statistics() { stats_.reset(); }

private:
    void handle_connection_state(transport::mqtt::ConnectionState state,
                                 const std::string& reason) {
        if (state == transport::mqtt::ConnectionState::CONNECTED) {
            connected_.store(true);
            IPB_LOG_INFO(LOG_CAT, "Connected to MQTT broker");

            // Re-publish birth on reconnect
            if (running_.load()) {
                rebirth();
            }
        } else if (state == transport::mqtt::ConnectionState::DISCONNECTED ||
                   state == transport::mqtt::ConnectionState::FAILED) {
            connected_.store(false);
            IPB_LOG_WARN(LOG_CAT, "Disconnected from MQTT broker: " << reason);
        }
    }

    void handle_message(const std::string& topic, const std::string& payload) {
        // Check for NCMD (rebirth request)
        if (topic.find("/NCMD/") != std::string::npos) {
            IPB_LOG_INFO(LOG_CAT, "Received NCMD on: " << topic);

            // Check for rebirth request
            // In real implementation, decode protobuf and check for Node Control/Rebirth metric
            if (config_.host.auto_rebirth_on_request) {
                rebirth();
            }
        }
    }

    void setup_death_certificate() {
        // Build NDEATH topic: spBv1.0/{group_id}/NDEATH/{edge_node_id}
        std::string death_topic =
            "spBv1.0/" + config_.edge_node.group_id + "/NDEATH/" + config_.edge_node.edge_node_id;

        // Build death payload (in real impl, use protobuf)
        std::string death_payload = build_death_payload();

        // Configure LWT via MQTT config (must be done before connect)
        config_.mqtt_config.lwt.enabled  = true;
        config_.mqtt_config.lwt.topic    = death_topic;
        config_.mqtt_config.lwt.payload  = death_payload;
        config_.mqtt_config.lwt.qos      = config_.publishing.death_qos;
        config_.mqtt_config.lwt.retained = false;
        config_.mqtt_config.sync_lwt();

        IPB_LOG_DEBUG(LOG_CAT, "Configured NDEATH as LWT on topic: " << death_topic);
    }

    void subscribe_to_commands() {
        // Subscribe to NCMD: spBv1.0/{group_id}/NCMD/{edge_node_id}
        std::string ncmd_topic =
            "spBv1.0/" + config_.edge_node.group_id + "/NCMD/" + config_.edge_node.edge_node_id;
        connection_->subscribe(ncmd_topic, config_.publishing.birth_qos);
        IPB_LOG_DEBUG(LOG_CAT, "Subscribed to NCMD: " << ncmd_topic);
    }

    common::Result<void> publish_birth() {
        IPB_SPAN_CAT("SparkplugSink::publish_birth", LOG_CAT);

        // Build NBIRTH topic: spBv1.0/{group_id}/NBIRTH/{edge_node_id}
        std::string birth_topic =
            "spBv1.0/" + config_.edge_node.group_id + "/NBIRTH/" + config_.edge_node.edge_node_id;

        // Build birth payload (in real impl, use protobuf)
        std::string birth_payload = build_birth_payload();

        bool success =
            connection_->publish_sync(birth_topic, birth_payload, config_.publishing.birth_qos,
                                      false, std::chrono::seconds{5});

        if (success) {
            stats_.births_sent++;
            stats_.bytes_sent += birth_payload.size();
            IPB_LOG_INFO(LOG_CAT, "Published NBIRTH to: " << birth_topic);

            // Publish DBIRTH for each device
            std::lock_guard<std::mutex> lock(devices_mutex_);
            for (const auto& [device_id, _] : devices_) {
                publish_device_birth(device_id);
            }

            return common::Result<void>{};
        } else {
            stats_.publish_failures++;
            IPB_LOG_ERROR(LOG_CAT, "Failed to publish NBIRTH");
            return common::Result<void>(common::ErrorCode::INTERNAL_ERROR,
                                        "Failed to publish NBIRTH");
        }
    }

    common::Result<void> publish_device_birth(const std::string& device_id) {
        // Build DBIRTH topic: spBv1.0/{group_id}/DBIRTH/{edge_node_id}/{device_id}
        std::string dbirth_topic = "spBv1.0/" + config_.edge_node.group_id + "/DBIRTH/" +
                                   config_.edge_node.edge_node_id + "/" + device_id;

        std::string dbirth_payload = build_device_birth_payload(device_id);

        bool success =
            connection_->publish_sync(dbirth_topic, dbirth_payload, config_.publishing.birth_qos,
                                      false, std::chrono::seconds{5});

        if (success) {
            stats_.births_sent++;
            stats_.bytes_sent += dbirth_payload.size();
            IPB_LOG_INFO(LOG_CAT, "Published DBIRTH for device: " << device_id);
            return common::Result<void>{};
        } else {
            stats_.publish_failures++;
            return common::Result<void>(common::ErrorCode::INTERNAL_ERROR,
                                        "Failed to publish DBIRTH");
        }
    }

    void publish_device_death(const std::string& device_id) {
        // Build DDEATH topic
        std::string ddeath_topic = "spBv1.0/" + config_.edge_node.group_id + "/DDEATH/" +
                                   config_.edge_node.edge_node_id + "/" + device_id;

        std::string ddeath_payload = build_device_death_payload(device_id);

        connection_->publish(ddeath_topic, ddeath_payload, config_.publishing.death_qos, false);

        stats_.deaths_sent++;
        IPB_LOG_INFO(LOG_CAT, "Published DDEATH for device: " << device_id);
    }

    void worker_loop() {
        while (running_.load()) {
            common::DataPoint dp;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock,
                               [this] { return !message_queue_.empty() || !running_.load(); });

                if (!running_.load())
                    break;

                if (!message_queue_.empty()) {
                    dp = std::move(message_queue_.front());
                    message_queue_.pop();
                } else {
                    continue;
                }
            }

            if (config_.publishing.enable_batching) {
                // Add to batch
                std::lock_guard<std::mutex> lock(batch_mutex_);
                current_batch_.push_back(std::move(dp));

                if (current_batch_.size() >= config_.publishing.batch_size) {
                    flush_batch_internal();
                }
            } else {
                // Publish immediately
                publish_data_point(dp);
            }
        }
    }

    void batch_loop() {
        while (running_.load()) {
            std::this_thread::sleep_for(config_.publishing.batch_timeout);

            if (running_.load()) {
                std::lock_guard<std::mutex> lock(batch_mutex_);
                if (!current_batch_.empty()) {
                    flush_batch_internal();
                }
            }
        }
    }

    void flush_batch() {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        flush_batch_internal();
    }

    void flush_batch_internal() {
        if (current_batch_.empty())
            return;

        // Build NDATA message with all metrics
        publish_data_batch(current_batch_);
        current_batch_.clear();
    }

    void publish_data_point(const common::DataPoint& dp) {
        // Determine if this is node data or device data
        std::string device_id = find_device_for_address(dp.address());

        std::string topic;
        if (device_id.empty()) {
            // Node data: spBv1.0/{group_id}/NDATA/{edge_node_id}
            topic = "spBv1.0/" + config_.edge_node.group_id + "/NDATA/" +
                    config_.edge_node.edge_node_id;
        } else {
            // Device data: spBv1.0/{group_id}/DDATA/{edge_node_id}/{device_id}
            topic = "spBv1.0/" + config_.edge_node.group_id + "/DDATA/" +
                    config_.edge_node.edge_node_id + "/" + device_id;
        }

        std::string payload = build_data_payload({dp});

        int token = connection_->publish(topic, payload, config_.publishing.data_qos, false);

        if (token >= 0) {
            stats_.data_messages_sent++;
            stats_.metrics_published++;
            stats_.bytes_sent += payload.size();
            stats_.sequence_number.store(sequence_number_.load());

            // Increment sequence number (wraps at 256)
            uint64_t seq = sequence_number_.fetch_add(1);
            if (seq >= 255) {
                sequence_number_.store(0);
            }
        } else {
            stats_.publish_failures++;
        }
    }

    void publish_data_batch(const std::vector<common::DataPoint>& batch) {
        if (batch.empty())
            return;

        // Group by device
        std::unordered_map<std::string, std::vector<common::DataPoint>> by_device;

        for (const auto& dp : batch) {
            std::string device_id = find_device_for_address(dp.address());
            by_device[device_id].push_back(dp);
        }

        // Publish each group
        for (const auto& [device_id, points] : by_device) {
            std::string topic;
            if (device_id.empty()) {
                topic = "spBv1.0/" + config_.edge_node.group_id + "/NDATA/" +
                        config_.edge_node.edge_node_id;
            } else {
                topic = "spBv1.0/" + config_.edge_node.group_id + "/DDATA/" +
                        config_.edge_node.edge_node_id + "/" + device_id;
            }

            std::string payload = build_data_payload(points);

            int token = connection_->publish(topic, payload, config_.publishing.data_qos, false);

            if (token >= 0) {
                stats_.data_messages_sent++;
                stats_.metrics_published += points.size();
                stats_.bytes_sent += payload.size();

                uint64_t seq = sequence_number_.fetch_add(1);
                if (seq >= 255) {
                    sequence_number_.store(0);
                }
            } else {
                stats_.publish_failures++;
            }
        }
    }

    std::string find_device_for_address(std::string_view address) const {
        std::lock_guard<std::mutex> lock(devices_mutex_);

        for (const auto& [device_id, device] : devices_) {
            if (!device.address_prefix.empty() &&
                address.substr(0, device.address_prefix.size()) == device.address_prefix) {
                return device_id;
            }
        }

        return "";  // Node-level metric
    }

    // Payload builders (simplified - real impl would use protobuf)
    std::string build_birth_payload() {
        // In real implementation, use protobuf to encode Sparkplug B payload
        // For now, return a placeholder
        return "NBIRTH_PAYLOAD_PLACEHOLDER";
    }

    std::string build_death_payload() { return "NDEATH_PAYLOAD_PLACEHOLDER"; }

    std::string build_device_birth_payload(const std::string& device_id) {
        return "DBIRTH_PAYLOAD_" + device_id;
    }

    std::string build_device_death_payload(const std::string& device_id) {
        return "DDEATH_PAYLOAD_" + device_id;
    }

    std::string build_data_payload(const std::vector<common::DataPoint>& points) {
        // In real implementation, use protobuf
        return "NDATA_PAYLOAD_" + std::to_string(points.size()) + "_METRICS";
    }

    SparkplugSinkConfig config_;
    std::shared_ptr<transport::mqtt::MQTTConnection> connection_;

    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::atomic<uint64_t> sequence_number_;
    std::atomic<uint64_t> bdseq_;

    // Message queue
    std::queue<common::DataPoint> message_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Batching
    std::vector<common::DataPoint> current_batch_;
    std::mutex batch_mutex_;

    // Metrics registry
    std::unordered_map<std::string, MetricDefinition> node_metrics_;
    std::mutex metrics_mutex_;
    uint64_t next_alias_ = 1;

    // Devices
    std::unordered_map<std::string, DeviceConfig> devices_;
    mutable std::mutex devices_mutex_;

    // Threads
    std::vector<std::thread> worker_threads_;
    std::thread batch_thread_;

    // Statistics
    SparkplugSinkStatisticsInternal stats_;
};

//=============================================================================
// SparkplugSink Implementation
//=============================================================================

SparkplugSink::SparkplugSink(const SparkplugSinkConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

SparkplugSink::~SparkplugSink() = default;

common::Result<void> SparkplugSink::write(const common::DataPoint& data_point) {
    return impl_->write(data_point);
}

common::Result<void> SparkplugSink::write_batch(std::span<const common::DataPoint> data_points) {
    return impl_->write_batch(data_points);
}

common::Result<void> SparkplugSink::write_dataset(const common::DataSet& dataset) {
    return impl_->write_dataset(dataset);
}

std::future<common::Result<void>> SparkplugSink::write_async(const common::DataPoint& data_point) {
    return impl_->write_async(data_point);
}

std::future<common::Result<void>> SparkplugSink::write_batch_async(std::span<const common::DataPoint> data_points) {
    return impl_->write_batch_async(data_points);
}

common::Result<void> SparkplugSink::flush() {
    return impl_->flush();
}

size_t SparkplugSink::pending_count() const noexcept {
    return impl_->pending_count();
}

bool SparkplugSink::can_accept_data() const noexcept {
    return impl_->can_accept_data();
}

size_t SparkplugSink::max_batch_size() const noexcept {
    return impl_->max_batch_size();
}

common::Result<void> SparkplugSink::start() {
    return impl_->start();
}

common::Result<void> SparkplugSink::stop() {
    return impl_->stop();
}

bool SparkplugSink::is_running() const noexcept {
    return impl_->is_running();
}

common::Result<void> SparkplugSink::configure(const common::ConfigurationBase& /*config*/) {
    return common::Result<void>{};
}

std::unique_ptr<common::ConfigurationBase> SparkplugSink::get_configuration() const {
    return nullptr;
}

common::Statistics SparkplugSink::get_statistics() const noexcept {
    auto stats = impl_->get_sparkplug_statistics();
    common::Statistics result;
    result.total_messages      = stats.metrics_published;
    result.successful_messages = stats.metrics_published - stats.publish_failures;
    result.failed_messages     = stats.publish_failures;
    result.total_bytes         = stats.bytes_sent;
    return result;
}

void SparkplugSink::reset_statistics() noexcept {
    impl_->reset_statistics();
}

bool SparkplugSink::is_healthy() const noexcept {
    return impl_->is_healthy();
}

std::string SparkplugSink::get_health_status() const {
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

common::Result<void> SparkplugSink::rebirth() {
    return impl_->rebirth();
}

void SparkplugSink::add_node_metric(const MetricDefinition& metric) {
    impl_->add_node_metric(metric);
}

common::Result<void> SparkplugSink::add_device(const DeviceConfig& device) {
    return impl_->add_device(device);
}

common::Result<void> SparkplugSink::remove_device(const std::string& device_id) {
    return impl_->remove_device(device_id);
}

uint64_t SparkplugSink::get_sequence_number() const noexcept {
    return impl_->get_sequence_number();
}

uint64_t SparkplugSink::get_bdseq() const noexcept {
    return impl_->get_bdseq();
}

bool SparkplugSink::is_connected() const noexcept {
    return impl_->is_connected();
}

SparkplugSinkStatistics SparkplugSink::get_sparkplug_statistics() const {
    return impl_->get_sparkplug_statistics();
}

//=============================================================================
// SparkplugSinkFactory Implementation
//=============================================================================

std::unique_ptr<SparkplugSink> SparkplugSinkFactory::create(const std::string& broker_url,
                                                            const std::string& group_id,
                                                            const std::string& edge_node_id) {
    auto config                   = SparkplugSinkConfig::create_default(group_id, edge_node_id);
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<SparkplugSink>(config);
}

std::unique_ptr<SparkplugSink> SparkplugSinkFactory::create_with_devices(
    const std::string& broker_url, const std::string& group_id, const std::string& edge_node_id,
    const std::vector<DeviceConfig>& devices) {
    auto config                   = SparkplugSinkConfig::create_default(group_id, edge_node_id);
    config.mqtt_config.broker_url = broker_url;
    config.edge_node.devices      = devices;
    return std::make_unique<SparkplugSink>(config);
}

std::unique_ptr<SparkplugSink> SparkplugSinkFactory::create(const SparkplugSinkConfig& config) {
    return std::make_unique<SparkplugSink>(config);
}

std::unique_ptr<SparkplugSink> SparkplugSinkFactory::create_high_throughput(
    const std::string& broker_url, const std::string& group_id, const std::string& edge_node_id) {
    auto config = SparkplugSinkConfig::create_high_throughput(group_id, edge_node_id);
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<SparkplugSink>(config);
}

std::unique_ptr<SparkplugSink> SparkplugSinkFactory::create_reliable(
    const std::string& broker_url, const std::string& group_id, const std::string& edge_node_id) {
    auto config                   = SparkplugSinkConfig::create_reliable(group_id, edge_node_id);
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<SparkplugSink>(config);
}

}  // namespace ipb::sink::sparkplug
