#include "ipb/transport/mqtt/backends/paho_backend.hpp"
#include "ipb/transport/mqtt/mqtt_connection.hpp"  // For ConnectionConfig

#include <chrono>
#include <iostream>

namespace ipb::transport::mqtt {

//=============================================================================
// Callback Handler (Paho callback interface)
//=============================================================================

class PahoBackend::CallbackHandler : public ::mqtt::callback {
public:
    explicit CallbackHandler(PahoBackend& backend) : backend_(backend) {}

    void connected(const std::string& cause) override {
        backend_.notify_connection_state(ConnectionState::CONNECTED, cause);
    }

    void connection_lost(const std::string& cause) override {
        backend_.stats_.reconnect_count++;
        backend_.notify_connection_state(ConnectionState::DISCONNECTED, cause);
    }

    void message_arrived(::mqtt::const_message_ptr msg) override {
        std::lock_guard<std::mutex> lock(backend_.callback_mutex_);
        if (backend_.message_cb_) {
            auto payload = msg->get_payload();
            std::span<const uint8_t> payload_span(
                reinterpret_cast<const uint8_t*>(payload.data()),
                payload.size()
            );
            backend_.message_cb_(
                msg->get_topic(),
                payload_span,
                static_cast<QoS>(msg->get_qos()),
                msg->is_retained()
            );
        }
        backend_.stats_.messages_received++;
        backend_.stats_.bytes_received += msg->get_payload().size();
    }

    void delivery_complete(::mqtt::delivery_token_ptr tok) override {
        std::lock_guard<std::mutex> lock(backend_.callback_mutex_);
        if (backend_.delivery_cb_) {
            backend_.delivery_cb_(
                static_cast<uint16_t>(tok->get_message_id()),
                true
            );
        }
    }

private:
    PahoBackend& backend_;
};

//=============================================================================
// PahoBackend Implementation
//=============================================================================

PahoBackend::PahoBackend() = default;

PahoBackend::~PahoBackend() {
    if (is_connected()) {
        disconnect(1000);
    }
}

std::string_view PahoBackend::version() const noexcept {
    // Paho doesn't expose version easily, use compile-time constant
    return "1.3.x";
}

bool PahoBackend::initialize(const ConnectionConfig& config) {
    try {
        broker_url_ = config.broker_url;
        client_id_ = config.client_id;

        // Create async client
        client_ = std::make_unique<::mqtt::async_client>(
            broker_url_,
            client_id_
        );

        // Setup callback handler
        callback_handler_ = std::make_unique<CallbackHandler>(*this);
        client_->set_callback(*callback_handler_);

        // Build connect options
        connect_opts_ = std::make_unique<::mqtt::connect_options>();
        connect_opts_->set_clean_session(config.clean_session);
        connect_opts_->set_keep_alive_interval(
            std::chrono::seconds(config.keep_alive_seconds)
        );
        connect_opts_->set_automatic_reconnect(config.auto_reconnect);

        if (config.auto_reconnect) {
            connect_opts_->set_min_retry_interval(
                std::chrono::seconds(config.reconnect_delay_seconds)
            );
            connect_opts_->set_max_retry_interval(
                std::chrono::seconds(config.reconnect_delay_seconds * 4)
            );
        }

        // Credentials
        if (!config.username.empty()) {
            connect_opts_->set_user_name(config.username);
            connect_opts_->set_password(config.password);
        }

        // TLS setup if needed
        if (config.security != SecurityMode::NONE) {
            setup_ssl(config);
        }

        // Last Will and Testament
        if (!config.lwt_topic.empty()) {
            auto lwt = ::mqtt::message(
                config.lwt_topic,
                config.lwt_payload,
                static_cast<int>(config.lwt_qos),
                config.lwt_retained
            );
            connect_opts_->set_will(lwt);
        }

        state_.store(ConnectionState::DISCONNECTED);
        return true;

    } catch (const std::exception& e) {
        std::cerr << "PahoBackend::initialize failed: " << e.what() << std::endl;
        return false;
    }
}

void PahoBackend::setup_ssl(const ConnectionConfig& config) {
    auto ssl_opts = ::mqtt::ssl_options();

    if (!config.tls.ca_cert_path.empty()) {
        ssl_opts.set_trust_store(config.tls.ca_cert_path);
    }

    if (!config.tls.client_cert_path.empty()) {
        ssl_opts.set_key_store(config.tls.client_cert_path);
    }

    if (!config.tls.client_key_path.empty()) {
        ssl_opts.set_private_key(config.tls.client_key_path);
    }

    ssl_opts.set_verify(config.tls.verify_server);
    ssl_opts.set_enable_server_cert_auth(config.tls.verify_server);

    connect_opts_->set_ssl(ssl_opts);
}

bool PahoBackend::connect() {
    if (!client_) return false;

    try {
        state_.store(ConnectionState::CONNECTING);

        auto token = client_->connect(*connect_opts_);
        token->wait();

        if (client_->is_connected()) {
            state_.store(ConnectionState::CONNECTED);
            notify_connection_state(ConnectionState::CONNECTED, "Connected");
            return true;
        }

        state_.store(ConnectionState::FAILED);
        return false;

    } catch (const std::exception& e) {
        state_.store(ConnectionState::FAILED);
        notify_connection_state(ConnectionState::FAILED, e.what());
        return false;
    }
}

void PahoBackend::disconnect(uint32_t timeout_ms) {
    if (!client_ || !client_->is_connected()) {
        state_.store(ConnectionState::DISCONNECTED);
        return;
    }

    try {
        auto token = client_->disconnect();
        token->wait_for(std::chrono::milliseconds(timeout_ms));
        state_.store(ConnectionState::DISCONNECTED);
        notify_connection_state(ConnectionState::DISCONNECTED, "Disconnected");

    } catch (const std::exception& e) {
        state_.store(ConnectionState::DISCONNECTED);
        notify_connection_state(ConnectionState::DISCONNECTED, e.what());
    }
}

bool PahoBackend::is_connected() const noexcept {
    return client_ && client_->is_connected();
}

ConnectionState PahoBackend::state() const noexcept {
    return state_.load();
}

std::string_view PahoBackend::client_id() const noexcept {
    return client_id_;
}

uint16_t PahoBackend::publish(
    std::string_view topic,
    std::span<const uint8_t> payload,
    QoS qos,
    bool retained)
{
    if (!is_connected()) return 0;

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto msg = ::mqtt::make_message(
            std::string(topic),
            payload.data(),
            payload.size(),
            static_cast<int>(qos),
            retained
        );

        auto token = client_->publish(msg);
        uint16_t msg_token = next_token_.fetch_add(1);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        stats_.messages_sent++;
        stats_.bytes_sent += payload.size();
        stats_.total_publish_time_ns += duration.count();
        stats_.publish_count++;

        return msg_token;

    } catch (const std::exception& e) {
        stats_.messages_failed++;
        return 0;
    }
}

bool PahoBackend::publish_sync(
    std::string_view topic,
    std::span<const uint8_t> payload,
    QoS qos,
    bool retained,
    uint32_t timeout_ms)
{
    if (!is_connected()) return false;

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto msg = ::mqtt::make_message(
            std::string(topic),
            payload.data(),
            payload.size(),
            static_cast<int>(qos),
            retained
        );

        auto token = client_->publish(msg);
        bool success = token->wait_for(std::chrono::milliseconds(timeout_ms));

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        if (success) {
            stats_.messages_sent++;
            stats_.bytes_sent += payload.size();
        } else {
            stats_.messages_failed++;
        }

        stats_.total_publish_time_ns += duration.count();
        stats_.publish_count++;

        return success;

    } catch (const std::exception& e) {
        stats_.messages_failed++;
        return false;
    }
}

bool PahoBackend::subscribe(std::string_view topic, QoS qos) {
    if (!is_connected()) return false;

    try {
        auto token = client_->subscribe(std::string(topic), static_cast<int>(qos));
        token->wait();
        return true;

    } catch (const std::exception&) {
        return false;
    }
}

bool PahoBackend::unsubscribe(std::string_view topic) {
    if (!is_connected()) return false;

    try {
        auto token = client_->unsubscribe(std::string(topic));
        token->wait();
        return true;

    } catch (const std::exception&) {
        return false;
    }
}

void PahoBackend::set_connection_callback(ConnectionCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    connection_cb_ = std::move(cb);
}

void PahoBackend::set_message_callback(MessageCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_cb_ = std::move(cb);
}

void PahoBackend::set_delivery_callback(DeliveryCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    delivery_cb_ = std::move(cb);
}

void PahoBackend::notify_connection_state(ConnectionState new_state, std::string_view reason) {
    state_.store(new_state);

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (connection_cb_) {
        connection_cb_(new_state, reason);
    }
}

size_t PahoBackend::dynamic_memory_usage() const noexcept {
    // Estimate - Paho doesn't expose this directly
    size_t usage = sizeof(PahoBackend);
    usage += broker_url_.capacity();
    usage += client_id_.capacity();
    // Paho internal buffers are opaque
    usage += 64 * 1024;  // Rough estimate for Paho internals
    return usage;
}

} // namespace ipb::transport::mqtt
