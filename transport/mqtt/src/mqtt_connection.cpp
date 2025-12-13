#include "ipb/transport/mqtt/mqtt_connection.hpp"

#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>

#include "ipb/transport/mqtt/backends/mqtt_backend.hpp"

namespace ipb::transport::mqtt {

//=============================================================================
// ConnectionConfig Implementation
//=============================================================================

bool ConnectionConfig::is_valid() const noexcept {
    if (broker_url.empty())
        return false;
    if (keep_alive.count() <= 0)
        return false;
    if (connect_timeout.count() <= 0)
        return false;

    // Check if selected backend is available
    if (!is_backend_available(backend))
        return false;

    if (security != SecurityMode::NONE) {
        if (security == SecurityMode::TLS || security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.ca_cert_path.empty())
                return false;
        }
        if (security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.client_cert_path.empty() || tls.client_key_path.empty())
                return false;
        }
        if (security == SecurityMode::TLS_PSK) {
            if (tls.psk_identity.empty() || tls.psk_key.empty())
                return false;
        }
    }

    return true;
}

std::string ConnectionConfig::validation_error() const {
    if (broker_url.empty())
        return "Broker URL is empty";
    if (keep_alive.count() <= 0)
        return "Keep alive must be positive";
    if (connect_timeout.count() <= 0)
        return "Connect timeout must be positive";

    if (!is_backend_available(backend)) {
        return "Selected backend '" + std::string(backend_type_name(backend)) +
               "' is not available";
    }

    if (security != SecurityMode::NONE) {
        if (security == SecurityMode::TLS || security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.ca_cert_path.empty())
                return "CA certificate path required for TLS";
        }
        if (security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.client_cert_path.empty())
                return "Client certificate path required";
            if (tls.client_key_path.empty())
                return "Client key path required";
        }
        if (security == SecurityMode::TLS_PSK) {
            if (tls.psk_identity.empty())
                return "PSK identity required";
            if (tls.psk_key.empty())
                return "PSK key required";
        }
    }

    return "";
}

//=============================================================================
// MQTTConnection::Impl - Backend-agnostic implementation
//=============================================================================

class MQTTConnection::Impl {
public:
    explicit Impl(const ConnectionConfig& config)
        : config_(config), state_(ConnectionState::DISCONNECTED) {
        // Generate client ID if not provided
        client_id_ = config.client_id.empty() ? generate_client_id("ipb") : config.client_id;

        // Sync keep_alive_seconds from keep_alive duration
        config_.keep_alive_seconds      = static_cast<uint16_t>(config.keep_alive.count());
        config_.reconnect_delay_seconds = static_cast<uint32_t>(config.min_reconnect_delay.count());

        // Sync LWT fields
        config_.sync_lwt();

        // Update client_id in config
        config_.client_id = client_id_;

        // Create the appropriate backend
        backend_ = create_backend(config.backend);
        if (!backend_) {
            std::cerr << "Failed to create MQTT backend: " << backend_type_name(config.backend)
                      << std::endl;
            return;
        }

        // Initialize backend
        if (!backend_->initialize(config_)) {
            std::cerr << "Failed to initialize MQTT backend" << std::endl;
            backend_.reset();
            return;
        }

        // Setup callbacks
        backend_->set_connection_callback([this](ConnectionState state, std::string_view reason) {
            handle_connection_state(state, reason);
        });

        backend_->set_message_callback(
            [this](std::string_view topic, std::span<const uint8_t> payload, QoS qos,
                   bool retained) { handle_message(topic, payload, qos, retained); });

        backend_->set_delivery_callback(
            [this](uint16_t token, bool success) { handle_delivery(token, success); });
    }

    ~Impl() { disconnect(std::chrono::milliseconds{5000}); }

    bool connect() {
        if (!backend_)
            return false;

        if (state_ == ConnectionState::CONNECTED || state_ == ConnectionState::CONNECTING) {
            return true;
        }

        state_ = ConnectionState::CONNECTING;
        return backend_->connect();
    }

    void disconnect(std::chrono::milliseconds timeout) {
        if (!backend_ || state_ == ConnectionState::DISCONNECTED) {
            return;
        }

        backend_->disconnect(static_cast<uint32_t>(timeout.count()));
        state_ = ConnectionState::DISCONNECTED;
    }

    bool is_connected() const noexcept { return backend_ && backend_->is_connected(); }

    ConnectionState get_state() const noexcept { return state_; }

    std::string get_client_id() const { return client_id_; }

    BackendType get_backend_type() const noexcept {
        return backend_ ? backend_->type() : BackendType::PAHO;
    }

    IMQTTBackend* get_backend() noexcept { return backend_.get(); }

    int process_events(uint32_t timeout_ms) {
        return backend_ ? backend_->process_events(timeout_ms) : -1;
    }

    bool requires_event_loop() const noexcept {
        return backend_ && backend_->requires_event_loop();
    }

    int publish(const std::string& topic, const void* payload, size_t len, QoS qos, bool retained) {
        if (!is_connected()) {
            stats_.messages_failed++;
            return -1;
        }

        std::span<const uint8_t> payload_span(static_cast<const uint8_t*>(payload), len);

        uint16_t token = backend_->publish(topic, payload_span, qos, retained);

        if (token > 0) {
            stats_.messages_published++;
            stats_.bytes_sent += len;
            return static_cast<int>(token);
        }

        stats_.messages_failed++;
        return -1;
    }

    bool publish_sync(const std::string& topic, const void* payload, size_t len, QoS qos,
                      bool retained, std::chrono::milliseconds timeout) {
        if (!is_connected()) {
            stats_.messages_failed++;
            return false;
        }

        std::span<const uint8_t> payload_span(static_cast<const uint8_t*>(payload), len);

        bool success = backend_->publish_sync(topic, payload_span, qos, retained,
                                              static_cast<uint32_t>(timeout.count()));

        if (success) {
            stats_.messages_published++;
            stats_.bytes_sent += len;
        } else {
            stats_.messages_failed++;
        }

        return success;
    }

    bool subscribe(const std::string& topic, QoS qos) {
        if (!is_connected())
            return false;
        return backend_->subscribe(topic, qos);
    }

    bool unsubscribe(const std::string& topic) {
        if (!is_connected())
            return false;
        return backend_->unsubscribe(topic);
    }

    void set_connection_callback(ConnectionCallback cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        connection_cb_ = std::move(cb);
    }

    void set_message_callback(MessageCallback cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        message_cb_ = std::move(cb);
    }

    void set_delivery_callback(DeliveryCallback cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        delivery_cb_ = std::move(cb);
    }

    const MQTTConnection::Statistics& get_statistics() const noexcept { return stats_; }

    void reset_statistics() {
        stats_.reset();
        if (backend_) {
            backend_->reset_stats();
        }
    }

private:
    void handle_connection_state(ConnectionState state, std::string_view reason) {
        state_ = state;

        if (state == ConnectionState::CONNECTED) {
            stats_.connected_since = std::chrono::steady_clock::now();
        } else if (state == ConnectionState::RECONNECTING) {
            stats_.reconnect_count++;
        }

        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (connection_cb_) {
            connection_cb_(state, std::string(reason));
        }
    }

    void handle_message(std::string_view topic, std::span<const uint8_t> payload, QoS qos,
                        bool retained) {
        stats_.messages_received++;
        stats_.bytes_received += payload.size();

        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (message_cb_) {
            std::string payload_str(reinterpret_cast<const char*>(payload.data()), payload.size());
            message_cb_(std::string(topic), payload_str, qos, retained);
        }
    }

    void handle_delivery(uint16_t token, bool success) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (delivery_cb_) {
            delivery_cb_(static_cast<int>(token), success, success ? "" : "Delivery failed");
        }
    }

    ConnectionConfig config_;
    std::string client_id_;
    std::unique_ptr<IMQTTBackend> backend_;
    std::atomic<ConnectionState> state_;

    // Callbacks
    ConnectionCallback connection_cb_;
    MessageCallback message_cb_;
    DeliveryCallback delivery_cb_;
    mutable std::mutex callback_mutex_;

    // Statistics
    mutable MQTTConnection::Statistics stats_;
};

//=============================================================================
// MQTTConnection Public Methods
//=============================================================================

MQTTConnection::MQTTConnection(const ConnectionConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

MQTTConnection::~MQTTConnection() = default;

MQTTConnection::MQTTConnection(MQTTConnection&&) noexcept            = default;
MQTTConnection& MQTTConnection::operator=(MQTTConnection&&) noexcept = default;

bool MQTTConnection::connect() {
    return impl_->connect();
}

void MQTTConnection::disconnect(std::chrono::milliseconds timeout) {
    impl_->disconnect(timeout);
}

bool MQTTConnection::is_connected() const noexcept {
    return impl_->is_connected();
}

ConnectionState MQTTConnection::get_state() const noexcept {
    return impl_->get_state();
}

std::string MQTTConnection::get_client_id() const {
    return impl_->get_client_id();
}

BackendType MQTTConnection::get_backend_type() const noexcept {
    return impl_->get_backend_type();
}

IMQTTBackend* MQTTConnection::get_backend() noexcept {
    return impl_->get_backend();
}

int MQTTConnection::process_events(uint32_t timeout_ms) {
    return impl_->process_events(timeout_ms);
}

bool MQTTConnection::requires_event_loop() const noexcept {
    return impl_->requires_event_loop();
}

int MQTTConnection::publish(const std::string& topic, const std::string& payload, QoS qos,
                            bool retained) {
    return impl_->publish(topic, payload.data(), payload.size(), qos, retained);
}

int MQTTConnection::publish(const std::string& topic, const std::vector<uint8_t>& payload, QoS qos,
                            bool retained) {
    return impl_->publish(topic, payload.data(), payload.size(), qos, retained);
}

bool MQTTConnection::publish_sync(const std::string& topic, const std::string& payload, QoS qos,
                                  bool retained, std::chrono::milliseconds timeout) {
    return impl_->publish_sync(topic, payload.data(), payload.size(), qos, retained, timeout);
}

bool MQTTConnection::subscribe(const std::string& topic, QoS qos) {
    return impl_->subscribe(topic, qos);
}

bool MQTTConnection::subscribe(const std::vector<std::pair<std::string, QoS>>& topics) {
    for (const auto& [topic, qos] : topics) {
        if (!impl_->subscribe(topic, qos)) {
            return false;
        }
    }
    return true;
}

bool MQTTConnection::unsubscribe(const std::string& topic) {
    return impl_->unsubscribe(topic);
}

bool MQTTConnection::unsubscribe(const std::vector<std::string>& topics) {
    for (const auto& topic : topics) {
        if (!impl_->unsubscribe(topic)) {
            return false;
        }
    }
    return true;
}

void MQTTConnection::set_connection_callback(ConnectionCallback cb) {
    impl_->set_connection_callback(std::move(cb));
}

void MQTTConnection::set_message_callback(MessageCallback cb) {
    impl_->set_message_callback(std::move(cb));
}

void MQTTConnection::set_delivery_callback(DeliveryCallback cb) {
    impl_->set_delivery_callback(std::move(cb));
}

const MQTTConnection::Statistics& MQTTConnection::get_statistics() const noexcept {
    return impl_->get_statistics();
}

void MQTTConnection::reset_statistics() {
    impl_->reset_statistics();
}

//=============================================================================
// MQTTConnectionManager
//=============================================================================

MQTTConnectionManager& MQTTConnectionManager::instance() {
    static MQTTConnectionManager instance;
    return instance;
}

MQTTConnectionManager::~MQTTConnectionManager() {
    disconnect_all();
}

std::shared_ptr<MQTTConnection> MQTTConnectionManager::get_or_create(
    const std::string& connection_id, const ConnectionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
        return it->second;
    }

    auto conn                   = std::make_shared<MQTTConnection>(config);
    connections_[connection_id] = conn;
    return conn;
}

std::shared_ptr<MQTTConnection> MQTTConnectionManager::get(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(connection_id);
    return (it != connections_.end()) ? it->second : nullptr;
}

bool MQTTConnectionManager::has_connection(const std::string& connection_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.find(connection_id) != connections_.end();
}

void MQTTConnectionManager::remove(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(connection_id);
}

std::vector<std::string> MQTTConnectionManager::get_connection_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> ids;
    ids.reserve(connections_.size());
    for (const auto& [id, _] : connections_) {
        ids.push_back(id);
    }
    return ids;
}

size_t MQTTConnectionManager::connection_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

void MQTTConnectionManager::disconnect_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [id, conn] : connections_) {
        if (conn) {
            conn->disconnect();
        }
    }
    connections_.clear();
}

//=============================================================================
// Utility Functions
//=============================================================================

std::string generate_client_id(const std::string& prefix) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    ss << prefix << "_";

    // Generate 8 random hex characters
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }

    // Add timestamp for uniqueness
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    ss << "_" << std::hex << (ms & 0xFFFF);

    return ss.str();
}

std::optional<std::tuple<std::string, std::string, uint16_t>> parse_broker_url(
    const std::string& url) {
    // Simple regex for mqtt:// tcp:// ssl:// urls
    std::regex url_regex(R"(^(tcp|ssl|mqtt|mqtts|ws|wss)://([^:/]+)(?::(\d+))?$)");
    std::smatch match;

    if (!std::regex_match(url, match, url_regex)) {
        return std::nullopt;
    }

    std::string protocol = match[1].str();
    std::string host     = match[2].str();
    uint16_t port        = 1883;

    if (match[3].matched) {
        port = static_cast<uint16_t>(std::stoi(match[3].str()));
    } else {
        // Default ports
        if (protocol == "ssl" || protocol == "mqtts" || protocol == "wss") {
            port = 8883;
        }
    }

    return std::make_tuple(protocol, host, port);
}

std::string build_broker_url(const std::string& host, uint16_t port, bool use_tls) {
    std::stringstream ss;
    ss << (use_tls ? "ssl" : "tcp") << "://" << host << ":" << port;
    return ss.str();
}

}  // namespace ipb::transport::mqtt
