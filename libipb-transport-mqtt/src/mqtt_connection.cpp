#include "ipb/transport/mqtt/mqtt_connection.hpp"

#include <mqtt/async_client.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <regex>

namespace ipb::transport::mqtt {

//=============================================================================
// ConnectionConfig Implementation
//=============================================================================

bool ConnectionConfig::is_valid() const noexcept {
    if (broker_url.empty()) return false;
    if (keep_alive.count() <= 0) return false;
    if (connect_timeout.count() <= 0) return false;

    if (security != SecurityMode::NONE) {
        if (security == SecurityMode::TLS || security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.ca_cert_path.empty()) return false;
        }
        if (security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.client_cert_path.empty() || tls.client_key_path.empty()) return false;
        }
        if (security == SecurityMode::TLS_PSK) {
            if (tls.psk_identity.empty() || tls.psk_key.empty()) return false;
        }
    }

    return true;
}

std::string ConnectionConfig::validation_error() const {
    if (broker_url.empty()) return "Broker URL is empty";
    if (keep_alive.count() <= 0) return "Keep alive must be positive";
    if (connect_timeout.count() <= 0) return "Connect timeout must be positive";

    if (security != SecurityMode::NONE) {
        if (security == SecurityMode::TLS || security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.ca_cert_path.empty()) return "CA certificate path required for TLS";
        }
        if (security == SecurityMode::TLS_CLIENT_CERT) {
            if (tls.client_cert_path.empty()) return "Client certificate path required";
            if (tls.client_key_path.empty()) return "Client key path required";
        }
        if (security == SecurityMode::TLS_PSK) {
            if (tls.psk_identity.empty()) return "PSK identity required";
            if (tls.psk_key.empty()) return "PSK key required";
        }
    }

    return "";
}

//=============================================================================
// MQTTConnection::Impl
//=============================================================================

class MQTTConnection::Impl : public ::mqtt::callback, public ::mqtt::iaction_listener {
public:
    explicit Impl(const ConnectionConfig& config)
        : config_(config)
        , state_(ConnectionState::DISCONNECTED) {

        // Generate client ID if not provided
        std::string client_id = config.client_id;
        if (client_id.empty()) {
            client_id = generate_client_id("ipb");
        }
        client_id_ = client_id;

        // Create MQTT client
        client_ = std::make_unique<::mqtt::async_client>(
            config.broker_url,
            client_id,
            config.max_buffered
        );

        // Set this as the callback handler
        client_->set_callback(*this);

        // Setup connection options
        setup_connect_options();
    }

    ~Impl() {
        disconnect(std::chrono::milliseconds{5000});
    }

    bool connect() {
        if (state_ == ConnectionState::CONNECTED ||
            state_ == ConnectionState::CONNECTING) {
            return true;
        }

        try {
            state_ = ConnectionState::CONNECTING;
            client_->connect(conn_opts_, nullptr, *this);
            return true;
        } catch (const ::mqtt::exception& e) {
            state_ = ConnectionState::FAILED;
            invoke_connection_callback(ConnectionState::FAILED, e.what());
            return false;
        }
    }

    void disconnect(std::chrono::milliseconds timeout) {
        if (state_ == ConnectionState::DISCONNECTED) {
            return;
        }

        try {
            if (client_->is_connected()) {
                client_->disconnect(static_cast<int>(timeout.count()))->wait();
            }
            state_ = ConnectionState::DISCONNECTED;
        } catch (const ::mqtt::exception&) {
            // Ignore disconnection errors
        }
    }

    bool is_connected() const noexcept {
        return state_ == ConnectionState::CONNECTED && client_->is_connected();
    }

    ConnectionState get_state() const noexcept {
        return state_;
    }

    std::string get_client_id() const {
        return client_id_;
    }

    int publish(const std::string& topic,
                const void* payload,
                size_t len,
                QoS qos,
                bool retained) {
        if (!is_connected()) {
            stats_.messages_failed++;
            return -1;
        }

        try {
            auto msg = ::mqtt::make_message(topic, payload, len, static_cast<int>(qos), retained);
            auto tok = client_->publish(msg);
            stats_.messages_published++;
            stats_.bytes_sent += len;
            return tok->get_message_id();
        } catch (const ::mqtt::exception&) {
            stats_.messages_failed++;
            return -1;
        }
    }

    bool publish_sync(const std::string& topic,
                      const void* payload,
                      size_t len,
                      QoS qos,
                      bool retained,
                      std::chrono::milliseconds timeout) {
        if (!is_connected()) {
            stats_.messages_failed++;
            return false;
        }

        try {
            auto msg = ::mqtt::make_message(topic, payload, len, static_cast<int>(qos), retained);
            auto tok = client_->publish(msg);
            if (tok->wait_for(timeout)) {
                stats_.messages_published++;
                stats_.bytes_sent += len;
                return true;
            }
            stats_.messages_failed++;
            return false;
        } catch (const ::mqtt::exception&) {
            stats_.messages_failed++;
            return false;
        }
    }

    bool subscribe(const std::string& topic, QoS qos) {
        if (!is_connected()) return false;

        try {
            client_->subscribe(topic, static_cast<int>(qos));
            return true;
        } catch (const ::mqtt::exception&) {
            return false;
        }
    }

    bool subscribe(const std::vector<std::pair<std::string, QoS>>& topics) {
        if (!is_connected() || topics.empty()) return false;

        try {
            std::vector<std::string> topic_filters;
            std::vector<int> qos_levels;
            for (const auto& [topic, qos] : topics) {
                topic_filters.push_back(topic);
                qos_levels.push_back(static_cast<int>(qos));
            }
            client_->subscribe(topic_filters, qos_levels);
            return true;
        } catch (const ::mqtt::exception&) {
            return false;
        }
    }

    bool unsubscribe(const std::string& topic) {
        if (!is_connected()) return false;

        try {
            client_->unsubscribe(topic);
            return true;
        } catch (const ::mqtt::exception&) {
            return false;
        }
    }

    bool unsubscribe(const std::vector<std::string>& topics) {
        if (!is_connected() || topics.empty()) return false;

        try {
            client_->unsubscribe(topics);
            return true;
        } catch (const ::mqtt::exception&) {
            return false;
        }
    }

    void set_connection_callback(ConnectionCallback cb) {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        connection_cb_ = std::move(cb);
    }

    void set_message_callback(MessageCallback cb) {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        message_cb_ = std::move(cb);
    }

    void set_delivery_callback(DeliveryCallback cb) {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        delivery_cb_ = std::move(cb);
    }

    const Statistics& get_statistics() const noexcept {
        return stats_;
    }

    void reset_statistics() {
        stats_.reset();
    }

private:
    // mqtt::callback overrides
    void connected(const std::string& cause) override {
        state_ = ConnectionState::CONNECTED;
        stats_.connected_since = std::chrono::steady_clock::now();
        invoke_connection_callback(ConnectionState::CONNECTED, cause);
    }

    void connection_lost(const std::string& cause) override {
        state_ = ConnectionState::DISCONNECTED;
        invoke_connection_callback(ConnectionState::DISCONNECTED, cause);

        // Attempt reconnection if enabled
        if (config_.auto_reconnect) {
            stats_.reconnect_count++;
            state_ = ConnectionState::RECONNECTING;
            invoke_connection_callback(ConnectionState::RECONNECTING, "Auto-reconnecting");
        }
    }

    void message_arrived(::mqtt::const_message_ptr msg) override {
        stats_.messages_received++;
        stats_.bytes_received += msg->get_payload().size();

        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (message_cb_) {
            message_cb_(
                msg->get_topic(),
                msg->get_payload_str(),
                static_cast<QoS>(msg->get_qos()),
                msg->is_retained()
            );
        }
    }

    void delivery_complete(::mqtt::delivery_token_ptr tok) override {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (delivery_cb_) {
            delivery_cb_(tok->get_message_id(), true, "");
        }
    }

    // mqtt::iaction_listener overrides
    void on_failure(const ::mqtt::token& tok) override {
        if (state_ == ConnectionState::CONNECTING) {
            state_ = ConnectionState::FAILED;
            invoke_connection_callback(ConnectionState::FAILED, "Connection failed");
        }
    }

    void on_success(const ::mqtt::token& tok) override {
        // Connection successful - handled in connected() callback
    }

    void setup_connect_options() {
        conn_opts_.set_clean_session(config_.clean_session);
        conn_opts_.set_keep_alive_interval(static_cast<int>(config_.keep_alive.count()));
        conn_opts_.set_connect_timeout(static_cast<int>(config_.connect_timeout.count()));
        conn_opts_.set_automatic_reconnect(config_.auto_reconnect);

        if (config_.auto_reconnect) {
            conn_opts_.set_min_retry_interval(static_cast<int>(config_.min_reconnect_delay.count()));
            conn_opts_.set_max_retry_interval(static_cast<int>(config_.max_reconnect_delay.count()));
        }

        if (!config_.username.empty()) {
            conn_opts_.set_user_name(config_.username);
            conn_opts_.set_password(config_.password);
        }

        // Setup TLS if required
        if (config_.security != SecurityMode::NONE) {
            setup_ssl_options();
        }

        // Setup LWT if configured
        if (config_.lwt.enabled) {
            auto lwt = ::mqtt::message(
                config_.lwt.topic,
                config_.lwt.payload,
                static_cast<int>(config_.lwt.qos),
                config_.lwt.retained
            );
            conn_opts_.set_will_message(std::make_shared<::mqtt::message>(lwt));
        }
    }

    void setup_ssl_options() {
        ::mqtt::ssl_options ssl_opts;

        if (!config_.tls.ca_cert_path.empty()) {
            ssl_opts.set_trust_store(config_.tls.ca_cert_path);
        }

        if (!config_.tls.client_cert_path.empty()) {
            ssl_opts.set_key_store(config_.tls.client_cert_path);
        }

        if (!config_.tls.client_key_path.empty()) {
            ssl_opts.set_private_key(config_.tls.client_key_path);
        }

        ssl_opts.set_enable_server_cert_auth(config_.tls.verify_certificate);

        conn_opts_.set_ssl(ssl_opts);
    }

    void invoke_connection_callback(ConnectionState state, const std::string& reason) {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (connection_cb_) {
            connection_cb_(state, reason);
        }
    }

    ConnectionConfig config_;
    std::string client_id_;
    std::unique_ptr<::mqtt::async_client> client_;
    ::mqtt::connect_options conn_opts_;
    std::atomic<ConnectionState> state_;
    Statistics stats_;

    std::mutex cb_mutex_;
    ConnectionCallback connection_cb_;
    MessageCallback message_cb_;
    DeliveryCallback delivery_cb_;
};

//=============================================================================
// MQTTConnection Implementation
//=============================================================================

MQTTConnection::MQTTConnection(const ConnectionConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

MQTTConnection::~MQTTConnection() = default;

MQTTConnection::MQTTConnection(MQTTConnection&&) noexcept = default;
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

int MQTTConnection::publish(const std::string& topic,
                            const std::string& payload,
                            QoS qos,
                            bool retained) {
    return impl_->publish(topic, payload.data(), payload.size(), qos, retained);
}

int MQTTConnection::publish(const std::string& topic,
                            const std::vector<uint8_t>& payload,
                            QoS qos,
                            bool retained) {
    return impl_->publish(topic, payload.data(), payload.size(), qos, retained);
}

bool MQTTConnection::publish_sync(const std::string& topic,
                                  const std::string& payload,
                                  QoS qos,
                                  bool retained,
                                  std::chrono::milliseconds timeout) {
    return impl_->publish_sync(topic, payload.data(), payload.size(), qos, retained, timeout);
}

bool MQTTConnection::subscribe(const std::string& topic, QoS qos) {
    return impl_->subscribe(topic, qos);
}

bool MQTTConnection::subscribe(const std::vector<std::pair<std::string, QoS>>& topics) {
    return impl_->subscribe(topics);
}

bool MQTTConnection::unsubscribe(const std::string& topic) {
    return impl_->unsubscribe(topic);
}

bool MQTTConnection::unsubscribe(const std::vector<std::string>& topics) {
    return impl_->unsubscribe(topics);
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
// MQTTConnectionManager Implementation
//=============================================================================

MQTTConnectionManager& MQTTConnectionManager::instance() {
    static MQTTConnectionManager instance;
    return instance;
}

MQTTConnectionManager::~MQTTConnectionManager() {
    disconnect_all();
}

std::shared_ptr<MQTTConnection> MQTTConnectionManager::get_or_create(
    const std::string& connection_id,
    const ConnectionConfig& config) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
        return it->second;
    }

    auto conn = std::make_shared<MQTTConnection>(config);
    connections_[connection_id] = conn;
    return conn;
}

std::shared_ptr<MQTTConnection> MQTTConnectionManager::get(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
        return it->second;
    }
    return nullptr;
}

bool MQTTConnectionManager::has_connection(const std::string& connection_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.find(connection_id) != connections_.end();
}

void MQTTConnectionManager::remove(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
        // Only remove if this is the last reference
        if (it->second.use_count() == 1) {
            it->second->disconnect();
            connections_.erase(it);
        }
    }
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

    for (auto& [_, conn] : connections_) {
        conn->disconnect();
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

    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }

    return ss.str();
}

std::optional<std::tuple<std::string, std::string, uint16_t>> parse_broker_url(const std::string& url) {
    // Pattern: protocol://host:port
    std::regex url_regex(R"(^(tcp|ssl|ws|wss)://([^:]+):(\d+)$)");
    std::smatch match;

    if (std::regex_match(url, match, url_regex)) {
        std::string protocol = match[1].str();
        std::string host = match[2].str();
        uint16_t port = static_cast<uint16_t>(std::stoi(match[3].str()));
        return std::make_tuple(protocol, host, port);
    }

    return std::nullopt;
}

std::string build_broker_url(const std::string& host, uint16_t port, bool use_tls) {
    std::stringstream ss;
    ss << (use_tls ? "ssl" : "tcp") << "://" << host << ":" << port;
    return ss.str();
}

} // namespace ipb::transport::mqtt
