#include "ipb/scoop/mqtt/mqtt_scoop.hpp"

#include <json/json.h>
#include <sstream>
#include <algorithm>
#include <thread>
#include <condition_variable>

namespace ipb::scoop::mqtt {

//=============================================================================
// TopicMapping Implementation
//=============================================================================

bool TopicMapping::matches(const std::string& topic) const {
    // Convert MQTT wildcards to regex
    std::string regex_pattern = topic_pattern;

    // Escape regex special characters (except our wildcards)
    for (size_t i = 0; i < regex_pattern.size(); ++i) {
        char c = regex_pattern[i];
        if (c == '.' || c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '^' || c == '$' || c == '|' ||
            c == '\\' || c == '?' || c == '*') {
            if (c != '+' && c != '#') {
                regex_pattern.insert(i, "\\");
                ++i;
            }
        }
    }

    // Replace MQTT wildcards
    // + matches any single level
    size_t pos = 0;
    while ((pos = regex_pattern.find('+', pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, "[^/]+");
        pos += 5;
    }

    // # matches any remaining levels
    pos = 0;
    while ((pos = regex_pattern.find('#', pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".*");
        pos += 2;
    }

    try {
        std::regex re("^" + regex_pattern + "$");
        return std::regex_match(topic, re);
    } catch (...) {
        return false;
    }
}

std::string TopicMapping::generate_address(const std::string& topic) const {
    std::string address = address_template;

    // Replace {topic} with the full topic
    size_t pos = address.find("{topic}");
    if (pos != std::string::npos) {
        address.replace(pos, 7, topic);
    }

    // Replace {level0}, {level1}, etc. with topic levels
    std::vector<std::string> levels;
    std::stringstream ss(topic);
    std::string level;
    while (std::getline(ss, level, '/')) {
        levels.push_back(level);
    }

    for (size_t i = 0; i < levels.size(); ++i) {
        std::string placeholder = "{level" + std::to_string(i) + "}";
        pos = 0;
        while ((pos = address.find(placeholder, pos)) != std::string::npos) {
            address.replace(pos, placeholder.length(), levels[i]);
        }
    }

    return address;
}

//=============================================================================
// MQTTScoopConfig Implementation
//=============================================================================

bool MQTTScoopConfig::is_valid() const {
    if (!mqtt_config.is_valid()) return false;
    if (subscription.mappings.empty()) return false;
    return true;
}

std::string MQTTScoopConfig::validation_error() const {
    if (!mqtt_config.is_valid()) return mqtt_config.validation_error();
    if (subscription.mappings.empty()) return "No topic mappings configured";
    return "";
}

MQTTScoopConfig MQTTScoopConfig::create_default() {
    MQTTScoopConfig config;
    config.mqtt_config.broker_url = "tcp://localhost:1883";

    TopicMapping default_mapping;
    default_mapping.topic_pattern = "#";
    default_mapping.address_template = "mqtt/{topic}";
    default_mapping.format = PayloadFormat::RAW;
    config.subscription.mappings.push_back(default_mapping);

    return config;
}

MQTTScoopConfig MQTTScoopConfig::create_high_throughput() {
    MQTTScoopConfig config = create_default();

    config.processing.buffer_size = 50000;
    config.processing.flush_interval = std::chrono::milliseconds{10};
    config.subscription.default_qos = transport::mqtt::QoS::AT_MOST_ONCE;

    return config;
}

MQTTScoopConfig MQTTScoopConfig::create_json_topics(const std::vector<std::string>& topics) {
    MQTTScoopConfig config;
    config.mqtt_config.broker_url = "tcp://localhost:1883";

    for (const auto& topic : topics) {
        TopicMapping mapping;
        mapping.topic_pattern = topic;
        mapping.address_template = "mqtt/{topic}";
        mapping.format = PayloadFormat::JSON;
        mapping.json_value_path = "value";
        config.subscription.mappings.push_back(mapping);
    }

    return config;
}

//=============================================================================
// MQTTScoop::Impl
//=============================================================================

class MQTTScoop::Impl {
public:
    explicit Impl(const MQTTScoopConfig& config)
        : config_(config)
        , running_(false)
        , connected_(false) {}

    ~Impl() {
        stop();
    }

    common::Result<> start() {
        if (running_.load()) {
            return common::Result<>::success();
        }

        // Get or create shared MQTT connection
        auto& manager = transport::mqtt::MQTTConnectionManager::instance();
        connection_ = manager.get_or_create(config_.connection_id, config_.mqtt_config);

        if (!connection_) {
            return common::Result<>::failure("Failed to create MQTT connection");
        }

        // Setup message callback
        connection_->set_message_callback(
            [this](const std::string& topic, const std::string& payload,
                   transport::mqtt::QoS qos, bool retained) {
                handle_message(topic, payload, retained);
            }
        );

        // Setup connection callback
        connection_->set_connection_callback(
            [this](transport::mqtt::ConnectionState state, const std::string& reason) {
                handle_connection_state(state, reason);
            }
        );

        // Connect
        if (!connection_->connect()) {
            return common::Result<>::failure("Failed to connect to MQTT broker");
        }

        running_.store(true);

        // Start processing thread
        processing_thread_ = std::thread(&Impl::processing_loop, this);

        // Subscribe to topics
        subscribe_all();

        return common::Result<>::success();
    }

    common::Result<> stop() {
        if (!running_.load()) {
            return common::Result<>::success();
        }

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

        // Unsubscribe and disconnect
        if (connection_) {
            for (const auto& mapping : config_.subscription.mappings) {
                connection_->unsubscribe(mapping.topic_pattern);
            }
            // Don't disconnect - shared connection may be used by others
        }

        connected_.store(false);
        return common::Result<>::success();
    }

    bool is_running() const noexcept { return running_.load(); }
    bool is_connected() const noexcept {
        return connected_.load() && connection_ && connection_->is_connected();
    }

    common::Result<common::DataSet> read() {
        std::lock_guard<std::mutex> lock(buffer_mutex_);

        common::DataSet result;
        while (!data_buffer_.empty()) {
            result.add(data_buffer_.front());
            data_buffer_.pop();
        }

        return common::Result<common::DataSet>::success(std::move(result));
    }

    common::Result<> subscribe(DataCallback data_cb, ErrorCallback error_cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        data_callback_ = std::move(data_cb);
        error_callback_ = std::move(error_cb);
        return common::Result<>::success();
    }

    common::Result<> unsubscribe() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        data_callback_ = nullptr;
        error_callback_ = nullptr;
        return common::Result<>::success();
    }

    common::Result<> add_topic_mapping(const TopicMapping& mapping) {
        {
            std::lock_guard<std::mutex> lock(mappings_mutex_);
            config_.subscription.mappings.push_back(mapping);
        }

        if (connection_ && connection_->is_connected()) {
            connection_->subscribe(mapping.topic_pattern, config_.subscription.default_qos);
            stats_.subscriptions_active++;
        }

        return common::Result<>::success();
    }

    common::Result<> remove_topic_mapping(const std::string& topic_pattern) {
        {
            std::lock_guard<std::mutex> lock(mappings_mutex_);
            auto& mappings = config_.subscription.mappings;
            mappings.erase(
                std::remove_if(mappings.begin(), mappings.end(),
                    [&](const TopicMapping& m) { return m.topic_pattern == topic_pattern; }),
                mappings.end()
            );
        }

        if (connection_ && connection_->is_connected()) {
            connection_->unsubscribe(topic_pattern);
            stats_.subscriptions_active--;
        }

        return common::Result<>::success();
    }

    std::vector<TopicMapping> get_topic_mappings() const {
        std::lock_guard<std::mutex> lock(mappings_mutex_);
        return config_.subscription.mappings;
    }

    void set_custom_parser(CustomParserCallback parser) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        custom_parser_ = std::move(parser);
    }

    MQTTScoopStatistics get_mqtt_statistics() const {
        return stats_;
    }

    std::vector<std::string> get_subscribed_topics() const {
        std::lock_guard<std::mutex> lock(mappings_mutex_);
        std::vector<std::string> topics;
        for (const auto& mapping : config_.subscription.mappings) {
            topics.push_back(mapping.topic_pattern);
        }
        return topics;
    }

    bool is_healthy() const noexcept {
        if (!running_.load() || !is_connected()) return false;
        return stats_.parse_errors.load() < config_.processing.max_parse_errors;
    }

    void reset_statistics() {
        stats_.reset();
    }

private:
    void subscribe_all() {
        std::lock_guard<std::mutex> lock(mappings_mutex_);
        for (const auto& mapping : config_.subscription.mappings) {
            if (connection_->subscribe(mapping.topic_pattern, config_.subscription.default_qos)) {
                stats_.subscriptions_active++;
            }
        }
    }

    void handle_connection_state(transport::mqtt::ConnectionState state, const std::string& reason) {
        if (state == transport::mqtt::ConnectionState::CONNECTED) {
            connected_.store(true);
            subscribe_all();
        } else if (state == transport::mqtt::ConnectionState::DISCONNECTED ||
                   state == transport::mqtt::ConnectionState::FAILED) {
            connected_.store(false);
            stats_.subscriptions_active.store(0);
        }
    }

    void handle_message(const std::string& topic, const std::string& payload, bool retained) {
        stats_.messages_received++;
        stats_.bytes_received += payload.size();

        // Check retained filter
        if (retained && config_.subscription.ignore_retained) {
            stats_.messages_dropped++;
            return;
        }

        // Check payload size
        if (payload.size() > config_.subscription.max_payload_size) {
            stats_.messages_dropped++;
            return;
        }

        // Find matching mapping
        TopicMapping* mapping = nullptr;
        {
            std::lock_guard<std::mutex> lock(mappings_mutex_);
            for (auto& m : config_.subscription.mappings) {
                if (m.matches(topic)) {
                    mapping = &m;
                    break;
                }
            }
        }

        if (!mapping) {
            stats_.messages_dropped++;
            return;
        }

        // Parse payload and create DataPoints
        auto data_points = parse_payload(topic, payload, *mapping);

        if (data_points.empty()) {
            if (!config_.processing.skip_parse_errors) {
                stats_.parse_errors++;
            }
            return;
        }

        stats_.messages_processed++;
        stats_.data_points_produced += data_points.size();

        // Buffer or deliver
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            for (auto& dp : data_points) {
                if (data_buffer_.size() < config_.processing.buffer_size) {
                    data_buffer_.push(std::move(dp));
                } else {
                    stats_.messages_dropped++;
                }
            }
            buffer_cv_.notify_one();
        }
    }

    std::vector<common::DataPoint> parse_payload(
        const std::string& topic,
        const std::string& payload,
        const TopicMapping& mapping)
    {
        std::vector<common::DataPoint> result;

        try {
            switch (mapping.format) {
                case PayloadFormat::RAW:
                    result.push_back(create_datapoint(
                        mapping.generate_address(topic),
                        payload,
                        mapping.protocol_id ? mapping.protocol_id : PROTOCOL_ID
                    ));
                    break;

                case PayloadFormat::JSON:
                    result = parse_json(topic, payload, mapping);
                    break;

                case PayloadFormat::BINARY_FLOAT:
                    if (payload.size() >= sizeof(float)) {
                        float value;
                        std::memcpy(&value, payload.data(), sizeof(float));
                        result.push_back(create_datapoint(
                            mapping.generate_address(topic),
                            static_cast<double>(value),
                            mapping.protocol_id ? mapping.protocol_id : PROTOCOL_ID
                        ));
                    }
                    break;

                case PayloadFormat::BINARY_DOUBLE:
                    if (payload.size() >= sizeof(double)) {
                        double value;
                        std::memcpy(&value, payload.data(), sizeof(double));
                        result.push_back(create_datapoint(
                            mapping.generate_address(topic),
                            value,
                            mapping.protocol_id ? mapping.protocol_id : PROTOCOL_ID
                        ));
                    }
                    break;

                case PayloadFormat::BINARY_INT32:
                    if (payload.size() >= sizeof(int32_t)) {
                        int32_t value;
                        std::memcpy(&value, payload.data(), sizeof(int32_t));
                        result.push_back(create_datapoint(
                            mapping.generate_address(topic),
                            static_cast<int64_t>(value),
                            mapping.protocol_id ? mapping.protocol_id : PROTOCOL_ID
                        ));
                    }
                    break;

                case PayloadFormat::BINARY_INT64:
                    if (payload.size() >= sizeof(int64_t)) {
                        int64_t value;
                        std::memcpy(&value, payload.data(), sizeof(int64_t));
                        result.push_back(create_datapoint(
                            mapping.generate_address(topic),
                            value,
                            mapping.protocol_id ? mapping.protocol_id : PROTOCOL_ID
                        ));
                    }
                    break;

                case PayloadFormat::CUSTOM:
                    {
                        std::lock_guard<std::mutex> lock(callback_mutex_);
                        if (custom_parser_) {
                            std::vector<uint8_t> bytes(payload.begin(), payload.end());
                            result = custom_parser_(topic, bytes);
                        }
                    }
                    break;

                default:
                    // Unsupported format
                    break;
            }
        } catch (...) {
            stats_.parse_errors++;
        }

        return result;
    }

    std::vector<common::DataPoint> parse_json(
        const std::string& topic,
        const std::string& payload,
        const TopicMapping& mapping)
    {
        std::vector<common::DataPoint> result;

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(payload);

        if (!Json::parseFromStream(builder, stream, &root, &errors)) {
            stats_.parse_errors++;
            return result;
        }

        // Extract value from JSON path
        Json::Value value = root;
        if (!mapping.json_value_path.empty()) {
            std::stringstream path_stream(mapping.json_value_path);
            std::string segment;
            while (std::getline(path_stream, segment, '.')) {
                if (value.isObject() && value.isMember(segment)) {
                    value = value[segment];
                } else {
                    return result;  // Path not found
                }
            }
        }

        auto dp = create_datapoint_from_json(
            mapping.generate_address(topic),
            value,
            mapping.protocol_id ? mapping.protocol_id : PROTOCOL_ID
        );

        if (dp) {
            result.push_back(std::move(*dp));
        }

        return result;
    }

    common::DataPoint create_datapoint(
        const std::string& address,
        const common::DataPoint::ValueType& value,
        uint16_t protocol_id)
    {
        common::DataPoint dp;
        dp.set_address(address);
        dp.set_value(value);
        dp.set_protocol_id(protocol_id);
        dp.set_quality(config_.processing.default_quality);
        dp.set_timestamp(std::chrono::system_clock::now());
        return dp;
    }

    std::optional<common::DataPoint> create_datapoint_from_json(
        const std::string& address,
        const Json::Value& value,
        uint16_t protocol_id)
    {
        common::DataPoint dp;
        dp.set_address(address);
        dp.set_protocol_id(protocol_id);
        dp.set_quality(config_.processing.default_quality);
        dp.set_timestamp(std::chrono::system_clock::now());

        if (value.isBool()) {
            dp.set_value(value.asBool());
        } else if (value.isInt64()) {
            dp.set_value(value.asInt64());
        } else if (value.isDouble()) {
            dp.set_value(value.asDouble());
        } else if (value.isString()) {
            dp.set_value(value.asString());
        } else {
            return std::nullopt;
        }

        return dp;
    }

    void processing_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lock(buffer_mutex_);

            buffer_cv_.wait_for(lock, config_.processing.flush_interval, [this] {
                return !data_buffer_.empty() || !running_.load();
            });

            if (!running_.load()) break;

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
                        ds.add(std::move(dp));
                    }
                    data_callback_(std::move(ds));
                }
            }
        }
    }

    MQTTScoopConfig config_;
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
    CustomParserCallback custom_parser_;
    mutable std::mutex callback_mutex_;

    // Mappings
    mutable std::mutex mappings_mutex_;

    // Processing thread
    std::thread processing_thread_;

    // Statistics
    MQTTScoopStatistics stats_;
};

//=============================================================================
// MQTTScoop Implementation
//=============================================================================

MQTTScoop::MQTTScoop(const MQTTScoopConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

MQTTScoop::~MQTTScoop() = default;

common::Result<common::DataSet> MQTTScoop::read() {
    return impl_->read();
}

common::Result<common::DataSet> MQTTScoop::read_async() {
    return impl_->read();
}

common::Result<> MQTTScoop::subscribe(DataCallback data_cb, ErrorCallback error_cb) {
    return impl_->subscribe(std::move(data_cb), std::move(error_cb));
}

common::Result<> MQTTScoop::unsubscribe() {
    return impl_->unsubscribe();
}

common::Result<> MQTTScoop::add_address(std::string_view address) {
    TopicMapping mapping;
    mapping.topic_pattern = std::string(address);
    mapping.address_template = "mqtt/{topic}";
    return impl_->add_topic_mapping(mapping);
}

common::Result<> MQTTScoop::remove_address(std::string_view address) {
    return impl_->remove_topic_mapping(std::string(address));
}

std::vector<std::string> MQTTScoop::get_addresses() const {
    return impl_->get_subscribed_topics();
}

common::Result<> MQTTScoop::connect() {
    return impl_->start();
}

common::Result<> MQTTScoop::disconnect() {
    return impl_->stop();
}

bool MQTTScoop::is_connected() const noexcept {
    return impl_->is_connected();
}

common::Result<> MQTTScoop::start() {
    return impl_->start();
}

common::Result<> MQTTScoop::stop() {
    return impl_->stop();
}

bool MQTTScoop::is_running() const noexcept {
    return impl_->is_running();
}

common::Result<> MQTTScoop::configure(const common::ConfigurationBase& config) {
    // Configuration should be done via constructor
    return common::Result<>::success();
}

std::unique_ptr<common::ConfigurationBase> MQTTScoop::get_configuration() const {
    return nullptr;  // TODO: Implement configuration export
}

common::Statistics MQTTScoop::get_statistics() const noexcept {
    auto mqtt_stats = impl_->get_mqtt_statistics();
    common::Statistics stats;
    stats.messages_received = mqtt_stats.messages_received.load();
    stats.messages_processed = mqtt_stats.messages_processed.load();
    stats.messages_dropped = mqtt_stats.messages_dropped.load();
    stats.errors = mqtt_stats.parse_errors.load();
    return stats;
}

void MQTTScoop::reset_statistics() noexcept {
    impl_->reset_statistics();
}

bool MQTTScoop::is_healthy() const noexcept {
    return impl_->is_healthy();
}

std::string MQTTScoop::get_health_status() const {
    if (impl_->is_healthy()) {
        return "healthy";
    } else if (!impl_->is_running()) {
        return "stopped";
    } else if (!impl_->is_connected()) {
        return "disconnected";
    } else {
        return "unhealthy: too many parse errors";
    }
}

common::Result<> MQTTScoop::add_topic_mapping(const TopicMapping& mapping) {
    return impl_->add_topic_mapping(mapping);
}

common::Result<> MQTTScoop::remove_topic_mapping(const std::string& topic_pattern) {
    return impl_->remove_topic_mapping(topic_pattern);
}

std::vector<TopicMapping> MQTTScoop::get_topic_mappings() const {
    return impl_->get_topic_mappings();
}

void MQTTScoop::set_custom_parser(CustomParserCallback parser) {
    impl_->set_custom_parser(std::move(parser));
}

MQTTScoopStatistics MQTTScoop::get_mqtt_statistics() const {
    return impl_->get_mqtt_statistics();
}

std::vector<std::string> MQTTScoop::get_subscribed_topics() const {
    return impl_->get_subscribed_topics();
}

//=============================================================================
// MQTTScoopFactory Implementation
//=============================================================================

std::unique_ptr<MQTTScoop> MQTTScoopFactory::create(const std::string& broker_url) {
    auto config = MQTTScoopConfig::create_default();
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<MQTTScoop>(config);
}

std::unique_ptr<MQTTScoop> MQTTScoopFactory::create_for_topics(
    const std::string& broker_url,
    const std::vector<std::string>& topics)
{
    MQTTScoopConfig config;
    config.mqtt_config.broker_url = broker_url;

    for (const auto& topic : topics) {
        TopicMapping mapping;
        mapping.topic_pattern = topic;
        mapping.address_template = "mqtt/{topic}";
        mapping.format = PayloadFormat::RAW;
        config.subscription.mappings.push_back(mapping);
    }

    return std::make_unique<MQTTScoop>(config);
}

std::unique_ptr<MQTTScoop> MQTTScoopFactory::create_json(
    const std::string& broker_url,
    const std::vector<std::string>& topics,
    const std::string& value_path)
{
    auto config = MQTTScoopConfig::create_json_topics(topics);
    config.mqtt_config.broker_url = broker_url;

    for (auto& mapping : config.subscription.mappings) {
        mapping.json_value_path = value_path;
    }

    return std::make_unique<MQTTScoop>(config);
}

std::unique_ptr<MQTTScoop> MQTTScoopFactory::create(const MQTTScoopConfig& config) {
    return std::make_unique<MQTTScoop>(config);
}

std::unique_ptr<MQTTScoop> MQTTScoopFactory::create_high_throughput(const std::string& broker_url) {
    auto config = MQTTScoopConfig::create_high_throughput();
    config.mqtt_config.broker_url = broker_url;
    return std::make_unique<MQTTScoop>(config);
}

} // namespace ipb::scoop::mqtt
