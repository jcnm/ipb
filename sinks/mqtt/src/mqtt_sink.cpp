#include "ipb/sink/mqtt/mqtt_sink.hpp"

#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>

#include <zlib.h>

namespace ipb::sink::mqtt {

// MQTTSinkConfig preset implementations
MQTTSinkConfig MQTTSinkConfig::create_high_throughput() {
    MQTTSinkConfig config;

    // Optimize for throughput
    config.performance.enable_batching  = true;
    config.performance.batch_size       = 500;
    config.performance.batch_timeout    = std::chrono::milliseconds{2000};
    config.performance.enable_async     = true;
    config.performance.queue_size       = 50000;
    config.performance.thread_pool_size = 4;

    config.messages.qos                = QoS::AT_MOST_ONCE;
    config.messages.enable_compression = true;
    config.messages.format             = MQTTMessageFormat::JSON_COMPACT;

    return config;
}

MQTTSinkConfig MQTTSinkConfig::create_low_latency() {
    MQTTSinkConfig config;

    // Optimize for latency
    config.performance.enable_batching  = false;
    config.performance.enable_async     = true;
    config.performance.queue_size       = 1000;
    config.performance.thread_pool_size = 1;
    config.performance.flush_interval   = std::chrono::milliseconds{1};

    config.messages.qos    = QoS::AT_MOST_ONCE;
    config.messages.format = MQTTMessageFormat::JSON_COMPACT;

    return config;
}

MQTTSinkConfig MQTTSinkConfig::create_reliable() {
    MQTTSinkConfig config;

    // Optimize for reliability
    config.performance.enable_batching = true;
    config.performance.batch_size      = 50;
    config.performance.batch_timeout   = std::chrono::milliseconds{500};

    config.messages.qos    = QoS::EXACTLY_ONCE;
    config.messages.retain = true;

    config.connection.auto_reconnect         = true;
    config.connection.max_reconnect_attempts = -1;
    config.connection.clean_session          = false;

    return config;
}

MQTTSinkConfig MQTTSinkConfig::create_minimal() {
    MQTTSinkConfig config;

    // Minimal configuration
    config.performance.enable_batching  = false;
    config.performance.enable_async     = false;
    config.messages.format              = MQTTMessageFormat::JSON;
    config.messages.qos                 = QoS::AT_MOST_ONCE;
    config.monitoring.enable_statistics = false;

    return config;
}

// MQTTSinkStatistics implementation
void MQTTSinkStatistics::reset() {
    messages_sent.store(0);
    messages_failed.store(0);
    bytes_sent.store(0);
    connection_attempts.store(0);
    connection_failures.store(0);
    reconnections.store(0);

    std::lock_guard<std::mutex> lock(timing_mutex);
    publish_times.clear();
}

void MQTTSinkStatistics::update_publish_time(std::chrono::nanoseconds time) {
    std::lock_guard<std::mutex> lock(timing_mutex);
    publish_times.push_back(time);

    // Keep only last 1000 measurements
    if (publish_times.size() > 1000) {
        publish_times.erase(publish_times.begin(), publish_times.begin() + 500);
    }
}

std::chrono::nanoseconds MQTTSinkStatistics::get_average_publish_time() const {
    std::lock_guard<std::mutex> lock(timing_mutex);
    if (publish_times.empty())
        return std::chrono::nanoseconds{0};

    auto total = std::chrono::nanoseconds{0};
    for (const auto& time : publish_times) {
        total += time;
    }
    return total / publish_times.size();
}

std::chrono::nanoseconds MQTTSinkStatistics::get_p95_publish_time() const {
    std::lock_guard<std::mutex> lock(timing_mutex);
    if (publish_times.empty())
        return std::chrono::nanoseconds{0};

    auto sorted_times = publish_times;
    std::sort(sorted_times.begin(), sorted_times.end());
    size_t p95_index = static_cast<size_t>(sorted_times.size() * 0.95);
    return sorted_times[std::min(p95_index, sorted_times.size() - 1)];
}

std::chrono::nanoseconds MQTTSinkStatistics::get_p99_publish_time() const {
    std::lock_guard<std::mutex> lock(timing_mutex);
    if (publish_times.empty())
        return std::chrono::nanoseconds{0};

    auto sorted_times = publish_times;
    std::sort(sorted_times.begin(), sorted_times.end());
    size_t p99_index = static_cast<size_t>(sorted_times.size() * 0.99);
    return sorted_times[std::min(p99_index, sorted_times.size() - 1)];
}

double MQTTSinkStatistics::get_message_rate() const {
    auto total_messages = messages_sent.load() + messages_failed.load();
    if (total_messages == 0)
        return 0.0;

    auto now                = std::chrono::system_clock::now();
    auto first_message_time = last_connection_time.load();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - first_message_time);

    if (duration.count() == 0)
        return 0.0;
    return static_cast<double>(total_messages) / duration.count();
}

double MQTTSinkStatistics::get_error_rate() const {
    auto total_messages = messages_sent.load() + messages_failed.load();
    if (total_messages == 0)
        return 0.0;

    return static_cast<double>(messages_failed.load()) / total_messages;
}

// MQTTSink implementation
MQTTSink::MQTTSink(const MQTTSinkConfig& config) : config_(config) {
    if (config_.performance.enable_memory_pool) {
        memory_pool_ = std::make_unique<char[]>(config_.performance.memory_pool_size);
    }
}

MQTTSink::~MQTTSink() {
    if (running_.load()) {
        stop();
    }
    shutdown();
}

common::Result<void> MQTTSink::initialize(const std::string& config_path) {
    try {
        // Get or create shared MQTT connection from MQTTConnectionManager
        auto& manager = transport::mqtt::MQTTConnectionManager::instance();
        connection_   = manager.get_or_create(config_.connection_id, config_.connection);

        if (!connection_) {
            return common::Result<void>::failure("Failed to create MQTT connection");
        }

        // Setup callbacks
        connection_->set_connection_callback(
            [this](transport::mqtt::ConnectionState state, const std::string& reason) {
                handle_connection_state(state, reason);
            });

        connection_->set_delivery_callback(
            [this](int token, bool success, const std::string& error) {
                handle_delivery_complete(token, success, error);
            });

        return common::Result<void>::success();

    } catch (const std::exception& e) {
        return common::Result<void>::failure("Failed to initialize MQTT sink: " +
                                             std::string(e.what()));
    }
}

common::Result<void> MQTTSink::start() {
    if (running_.load()) {
        return common::Result<void>::failure("MQTT sink is already running");
    }

    try {
        // Connect to broker
        auto connect_result = connect_to_broker();
        if (!connect_result.is_success()) {
            return connect_result;
        }

        running_.store(true);
        shutdown_requested_.store(false);

        // Start worker threads
        if (config_.performance.enable_async) {
            for (size_t i = 0; i < config_.performance.thread_pool_size; ++i) {
                worker_threads_.emplace_back(&MQTTSink::worker_loop, this);
            }
        }

        // Start batch thread
        if (config_.performance.enable_batching) {
            batch_thread_    = std::thread(&MQTTSink::batch_loop, this);
            last_batch_time_ = std::chrono::steady_clock::now();
        }

        // Start statistics thread
        if (config_.monitoring.enable_statistics) {
            statistics_thread_ = std::thread(&MQTTSink::statistics_loop, this);
        }

        // Reset statistics
        statistics_.reset();
        statistics_.last_connection_time.store(std::chrono::system_clock::now());

        return common::Result<void>::success();

    } catch (const std::exception& e) {
        running_.store(false);
        return common::Result<void>::failure("Failed to start MQTT sink: " + std::string(e.what()));
    }
}

common::Result<void> MQTTSink::stop() {
    if (!running_.load()) {
        return common::Result<void>::success();
    }

    try {
        running_.store(false);

        // Notify all waiting threads
        queue_cv_.notify_all();

        // Wait for worker threads to finish
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads_.clear();

        // Stop batch thread
        if (batch_thread_.joinable()) {
            batch_thread_.join();
        }

        // Stop statistics thread
        if (statistics_thread_.joinable()) {
            statistics_thread_.join();
        }

        // Flush any remaining messages
        flush_current_batch();

        // Disconnect from broker
        auto disconnect_result = disconnect_from_broker();
        if (!disconnect_result.is_success()) {
            return disconnect_result;
        }

        return common::Result<void>::success();

    } catch (const std::exception& e) {
        return common::Result<void>::failure("Failed to stop MQTT sink: " + std::string(e.what()));
    }
}

common::Result<void> MQTTSink::shutdown() {
    shutdown_requested_.store(true);

    auto stop_result = stop();
    if (!stop_result.is_success()) {
        return stop_result;
    }

    try {
        // Note: Don't disconnect shared connection - other components may use it
        // The MQTTConnectionManager handles cleanup when all references are released
        connection_.reset();

        return common::Result<void>::success();

    } catch (const std::exception& e) {
        return common::Result<void>::failure("Failed to shutdown MQTT sink: " +
                                             std::string(e.what()));
    }
}

bool MQTTSink::is_connected() const {
    return connected_.load() && connection_ && connection_->is_connected();
}

bool MQTTSink::is_healthy() const {
    if (!running_.load() || !is_connected()) {
        return false;
    }

    // Check error rate
    auto error_rate = statistics_.get_error_rate();
    if (error_rate > config_.monitoring.max_error_rate) {
        return false;
    }

    // Check recent activity
    auto now          = std::chrono::system_clock::now();
    auto last_message = statistics_.last_message_time.load();
    auto time_since_last_message =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_message);

    // Consider healthy if we've sent a message in the last 5 minutes
    return time_since_last_message.count() < 300;
}

common::Result<void> MQTTSink::send_data_point(const common::DataPoint& data_point) {
    if (!running_.load()) {
        return common::Result<void>::failure("MQTT sink is not running");
    }

    try {
        if (config_.performance.enable_async) {
            // Add to queue for async processing
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (message_queue_.size() >= config_.performance.queue_size) {
                    if (config_.performance.enable_backpressure) {
                        return common::Result<void>::failure("Message queue is full");
                    } else {
                        // Drop oldest message
                        message_queue_.pop();
                    }
                }
                message_queue_.push(data_point);
            }
            queue_cv_.notify_one();

            return common::Result<void>::success();
        } else {
            // Synchronous processing
            return publish_data_point_internal(data_point);
        }

    } catch (const std::exception& e) {
        statistics_.messages_failed.fetch_add(1);
        return common::Result<void>::failure("Failed to send data point: " + std::string(e.what()));
    }
}

common::Result<void> MQTTSink::send_data_set(const common::DataSet& data_set) {
    if (!running_.load()) {
        return common::Result<void>::failure("MQTT sink is not running");
    }

    try {
        if (config_.performance.enable_batching) {
            // Send as batch
            auto batch_message = format_batch_message(data_set);
            auto topic         = config_.messages.base_topic + "/batch";

            return publish_message(topic, batch_message, config_.messages.qos,
                                   config_.messages.retain);
        } else {
            // Send individual data points
            for (const auto& data_point : data_set.get_data_points()) {
                auto result = send_data_point(data_point);
                if (!result.is_success()) {
                    return result;
                }
            }
            return common::Result<void>::success();
        }

    } catch (const std::exception& e) {
        statistics_.messages_failed.fetch_add(1);
        return common::Result<void>::failure("Failed to send data set: " + std::string(e.what()));
    }
}

common::SinkMetrics MQTTSink::get_metrics() const {
    common::SinkMetrics metrics;
    metrics.sink_id             = config_.sink_id;
    metrics.messages_sent       = statistics_.messages_sent.load();
    metrics.messages_failed     = statistics_.messages_failed.load();
    metrics.bytes_sent          = statistics_.bytes_sent.load();
    metrics.is_connected        = is_connected();
    metrics.is_healthy          = is_healthy();
    metrics.avg_processing_time = statistics_.get_average_publish_time();

    return metrics;
}

std::string MQTTSink::get_sink_info() const {
    Json::Value info;
    info["sink_type"]    = "mqtt";
    info["sink_id"]      = config_.sink_id;
    info["broker_url"]   = config_.connection.broker_url;
    info["client_id"]    = config_.connection.client_id;
    info["base_topic"]   = config_.messages.base_topic;
    info["is_connected"] = is_connected();
    info["is_healthy"]   = is_healthy();

    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, info);
}

// Private implementation methods
common::Result<void> MQTTSink::connect_to_broker() {
    try {
        statistics_.connection_attempts.fetch_add(1);

        if (!connection_) {
            statistics_.connection_failures.fetch_add(1);
            return common::Result<void>::failure("MQTT connection not initialized");
        }

        // Connect using shared transport
        if (!connection_->connect()) {
            statistics_.connection_failures.fetch_add(1);
            return common::Result<void>::failure("Failed to connect to MQTT broker");
        }

        // Wait a bit for connection to establish
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        if (!connection_->is_connected()) {
            statistics_.connection_failures.fetch_add(1);
            return common::Result<void>::failure("MQTT connection not established");
        }

        connected_.store(true);
        statistics_.is_connected.store(true);
        statistics_.last_connection_time.store(std::chrono::system_clock::now());

        return common::Result<void>::success();

    } catch (const std::exception& e) {
        statistics_.connection_failures.fetch_add(1);
        connected_.store(false);
        return common::Result<void>::failure("Exception during MQTT connection: " +
                                             std::string(e.what()));
    }
}

common::Result<void> MQTTSink::disconnect_from_broker() {
    try {
        // Note: Don't disconnect shared connection - just update our state
        // Other components may still be using the same connection
        connected_.store(false);
        statistics_.is_connected.store(false);

        return common::Result<void>::success();

    } catch (const std::exception& e) {
        return common::Result<void>::failure("Exception during MQTT disconnection: " +
                                             std::string(e.what()));
    }
}

void MQTTSink::handle_connection_state(transport::mqtt::ConnectionState state,
                                       const std::string& reason) {
    switch (state) {
        case transport::mqtt::ConnectionState::CONNECTED:
            connected_.store(true);
            statistics_.is_connected.store(true);
            statistics_.last_connection_time.store(std::chrono::system_clock::now());
            break;

        case transport::mqtt::ConnectionState::DISCONNECTED:
        case transport::mqtt::ConnectionState::FAILED:
            connected_.store(false);
            statistics_.is_connected.store(false);
            break;

        case transport::mqtt::ConnectionState::RECONNECTING:
            statistics_.reconnections.fetch_add(1);
            break;

        default:
            break;
    }
}

void MQTTSink::handle_delivery_complete(int token, bool success, const std::string& error) {
    if (!success) {
        statistics_.messages_failed.fetch_add(1);
    }
}

void MQTTSink::worker_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !message_queue_.empty() || !running_.load(); });

        if (!running_.load())
            break;

        if (!message_queue_.empty()) {
            auto data_point = message_queue_.front();
            message_queue_.pop();
            lock.unlock();

            publish_data_point_internal(data_point);
        }
    }
}

void MQTTSink::batch_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(config_.performance.flush_interval);

        if (should_flush_batch()) {
            flush_current_batch();
        }
    }
}

void MQTTSink::statistics_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(config_.monitoring.statistics_interval);

        if (running_.load() && config_.monitoring.enable_statistics) {
            print_statistics();
        }
    }
}

common::Result<void> MQTTSink::publish_data_point_internal(const common::DataPoint& data_point) {
    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        auto topic   = generate_topic(data_point);
        auto message = format_message(data_point);

        auto result =
            publish_message(topic, message, config_.messages.qos, config_.messages.retain);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto publish_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

        if (result.is_success()) {
            statistics_.messages_sent.fetch_add(1);
            statistics_.bytes_sent.fetch_add(message.size());
            statistics_.update_publish_time(publish_time);
            statistics_.last_message_time.store(std::chrono::system_clock::now());
        } else {
            statistics_.messages_failed.fetch_add(1);
        }

        return result;

    } catch (const std::exception& e) {
        statistics_.messages_failed.fetch_add(1);
        return common::Result<void>::failure("Exception during publish: " + std::string(e.what()));
    }
}

common::Result<void> MQTTSink::publish_message(const std::string& topic, const std::string& payload,
                                               QoS qos, bool retain) {
    if (!is_connected()) {
        return common::Result<void>::failure("MQTT client is not connected");
    }

    try {
        // Use shared transport to publish
        if (qos == QoS::AT_MOST_ONCE) {
            // Fire and forget
            int token = connection_->publish(topic, payload, qos, retain);
            if (token < 0) {
                return common::Result<void>::failure("Failed to publish message");
            }
        } else {
            // Wait for delivery confirmation for QoS 1 and 2
            bool success = connection_->publish_sync(topic, payload, qos, retain,
                                                     config_.performance.publish_timeout);
            if (!success) {
                return common::Result<void>::failure("Failed to publish message with confirmation");
            }
        }

        return common::Result<void>::success();

    } catch (const std::exception& e) {
        return common::Result<void>::failure("Failed to publish MQTT message: " +
                                             std::string(e.what()));
    }
}

std::string MQTTSink::generate_topic(const common::DataPoint& data_point) const {
    switch (config_.messages.topic_strategy) {
        case MQTTTopicStrategy::SINGLE_TOPIC:
            return generate_single_topic();

        case MQTTTopicStrategy::PROTOCOL_BASED:
            return generate_protocol_topic(data_point);

        case MQTTTopicStrategy::ADDRESS_BASED:
            return generate_address_topic(data_point);

        case MQTTTopicStrategy::HIERARCHICAL:
            return generate_hierarchical_topic(data_point);

        case MQTTTopicStrategy::CUSTOM:
            if (config_.messages.custom_topic_generator) {
                return config_.messages.custom_topic_generator(data_point);
            }
            return generate_single_topic();

        default:
            return generate_single_topic();
    }
}

std::string MQTTSink::format_message(const common::DataPoint& data_point) const {
    switch (config_.messages.format) {
        case MQTTMessageFormat::JSON:
            return Json::writeString(Json::StreamWriterBuilder{}, data_point_to_json(data_point));

        case MQTTMessageFormat::JSON_COMPACT: {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, data_point_to_json(data_point));
        }

        case MQTTMessageFormat::CSV:
            return data_point_to_csv(data_point);

        case MQTTMessageFormat::INFLUX_LINE:
            return data_point_to_influx_line(data_point);

        case MQTTMessageFormat::CUSTOM:
            if (config_.messages.custom_formatter) {
                return config_.messages.custom_formatter(data_point);
            }
            return Json::writeString(Json::StreamWriterBuilder{}, data_point_to_json(data_point));

        default:
            return Json::writeString(Json::StreamWriterBuilder{}, data_point_to_json(data_point));
    }
}

Json::Value MQTTSink::data_point_to_json(const common::DataPoint& data_point) const {
    Json::Value json;

    json["address"] = data_point.get_address();

    if (config_.messages.include_timestamp) {
        auto timestamp    = data_point.get_timestamp();
        auto time_t       = std::chrono::system_clock::to_time_t(timestamp);
        json["timestamp"] = static_cast<int64_t>(time_t);
    }

    if (config_.messages.include_protocol_info) {
        json["protocol_id"] = data_point.get_protocol_id();
    }

    if (config_.messages.include_quality) {
        json["quality"] = static_cast<int>(data_point.get_quality());
    }

    // Add value based on type
    auto value_variant = data_point.get_value();
    std::visit(
        [&json](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                json["value"] = value;
            } else if constexpr (std::is_arithmetic_v<T>) {
                json["value"] = value;
            } else if constexpr (std::is_same_v<T, std::string>) {
                json["value"] = value;
            }
        },
        value_variant);

    return json;
}

void MQTTSink::print_statistics() const {
    if (!config_.monitoring.enable_statistics) {
        return;
    }

    auto stats = get_statistics();

    std::cout << "MQTT Sink Statistics [" << config_.sink_id
              << "]: " << "sent=" << stats.messages_sent.load()
              << ", failed=" << stats.messages_failed.load()
              << ", bytes=" << stats.bytes_sent.load()
              << ", connected=" << (stats.is_connected.load() ? "true" : "false")
              << ", avg_time=" << stats.get_average_publish_time().count() << "ns"
              << ", p95_time=" << stats.get_p95_publish_time().count() << "ns"
              << ", error_rate=" << (stats.get_error_rate() * 100.0) << "%" << std::endl;
}

// Factory implementations
std::unique_ptr<MQTTSink> MQTTSinkFactory::create_high_throughput(const std::string& broker_url,
                                                                  const std::string& base_topic) {
    auto config                  = MQTTSinkConfig::create_high_throughput();
    config.connection.broker_url = broker_url;
    config.messages.base_topic   = base_topic;

    return std::make_unique<MQTTSink>(config);
}

std::unique_ptr<MQTTSink> MQTTSinkFactory::create_low_latency(const std::string& broker_url,
                                                              const std::string& base_topic) {
    auto config                  = MQTTSinkConfig::create_low_latency();
    config.connection.broker_url = broker_url;
    config.messages.base_topic   = base_topic;

    return std::make_unique<MQTTSink>(config);
}

std::unique_ptr<MQTTSink> MQTTSinkFactory::create_reliable(const std::string& broker_url,
                                                           const std::string& base_topic) {
    auto config                  = MQTTSinkConfig::create_reliable();
    config.connection.broker_url = broker_url;
    config.messages.base_topic   = base_topic;

    return std::make_unique<MQTTSink>(config);
}

std::unique_ptr<MQTTSink> MQTTSinkFactory::create(const MQTTSinkConfig& config) {
    return std::make_unique<MQTTSink>(config);
}

}  // namespace ipb::sink::mqtt
