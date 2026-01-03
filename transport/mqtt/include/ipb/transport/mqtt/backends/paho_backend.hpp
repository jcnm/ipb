#pragma once

/**
 * @file paho_backend.hpp
 * @brief Eclipse Paho MQTT backend implementation
 *
 * Default backend using Eclipse Paho MQTT C/C++ library.
 * Best for: General purpose, feature-complete, well-tested.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <mqtt/async_client.h>

#include "mqtt_backend.hpp"

namespace ipb::transport::mqtt {

// Forward declaration
struct ConnectionConfig;

/**
 * @brief Paho MQTT Backend
 *
 * Wraps Eclipse Paho async_client with the IMQTTBackend interface.
 * Uses internal threading model - no manual event processing required.
 */
class PahoBackend : public IMQTTBackend {
public:
    PahoBackend();
    ~PahoBackend() override;

    // Non-copyable, non-movable (due to callback registrations)
    PahoBackend(const PahoBackend&)            = delete;
    PahoBackend& operator=(const PahoBackend&) = delete;

    //=========================================================================
    // IMQTTBackend Implementation
    //=========================================================================

    BackendType type() const noexcept override { return BackendType::PAHO; }
    std::string_view name() const noexcept override { return "Eclipse Paho MQTT"; }
    std::string_view version() const noexcept override;

    bool initialize(const ConnectionConfig& config) override;
    bool connect() override;
    void disconnect(uint32_t timeout_ms = 5000) override;
    bool is_connected() const noexcept override;
    ConnectionState state() const noexcept override;
    std::string_view client_id() const noexcept override;

    uint16_t publish(std::string_view topic, std::span<const uint8_t> payload,
                     QoS qos = QoS::AT_LEAST_ONCE, bool retained = false) override;

    bool publish_sync(std::string_view topic, std::span<const uint8_t> payload, QoS qos,
                      bool retained, uint32_t timeout_ms) override;

    bool subscribe(std::string_view topic, QoS qos = QoS::AT_LEAST_ONCE) override;
    bool unsubscribe(std::string_view topic) override;

    void set_connection_callback(ConnectionCallback cb) override;
    void set_message_callback(MessageCallback cb) override;
    void set_delivery_callback(DeliveryCallback cb) override;

    // Paho uses internal threads - no event loop needed
    bool requires_event_loop() const noexcept override { return false; }

    const BackendStats& stats() const noexcept override { return stats_; }
    void reset_stats() noexcept override { stats_.reset(); }

    size_t dynamic_memory_usage() const noexcept override;

private:
    class CallbackHandler;

    std::unique_ptr<::mqtt::async_client> client_;
    std::unique_ptr<::mqtt::connect_options> connect_opts_;
    std::unique_ptr<CallbackHandler> callback_handler_;

    std::string client_id_;
    std::string broker_url_;
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED};

    // Callbacks
    ConnectionCallback connection_cb_;
    MessageCallback message_cb_;
    DeliveryCallback delivery_cb_;
    mutable std::mutex callback_mutex_;

    // Statistics
    mutable BackendStats stats_;

    // Message token counter
    std::atomic<uint16_t> next_token_{1};

    void setup_ssl(const ConnectionConfig& config);
    void notify_connection_state(ConnectionState new_state, std::string_view reason);
};

}  // namespace ipb::transport::mqtt
