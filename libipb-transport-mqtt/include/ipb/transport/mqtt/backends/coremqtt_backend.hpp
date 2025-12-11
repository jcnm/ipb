#pragma once

/**
 * @file coremqtt_backend.hpp
 * @brief AWS coreMQTT backend implementation
 *
 * Embedded-optimized backend using AWS coreMQTT library.
 * Characteristics:
 * - Zero dynamic allocation in library
 * - User provides all buffers (compile-time or runtime configurable)
 * - Single-threaded event loop model
 * - MQTT v5 compliant
 *
 * Best for: Low-latency, memory-constrained, deterministic timing.
 */

#include "mqtt_backend.hpp"

#include <array>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <memory>
#include <chrono>

// Forward declarations for coreMQTT types (avoid including C headers in hpp)
struct MQTTContext;
struct NetworkContext;

namespace ipb::transport::mqtt {

//=============================================================================
// Buffer Configuration
//=============================================================================

/**
 * @brief coreMQTT buffer sizes (can be overridden at compile time)
 */
struct CoreMQTTBufferConfig {
    static constexpr size_t NETWORK_BUFFER_SIZE = 4096;
    static constexpr size_t MAX_SUBSCRIPTIONS = 64;
    static constexpr size_t MAX_OUTGOING_PUBLISHES = 128;
    static constexpr size_t MAX_INCOMING_PUBLISHES = 128;
    static constexpr size_t TOPIC_FILTER_MAX_SIZE = 256;
};

//=============================================================================
// CoreMQTT Backend
//=============================================================================

/**
 * @brief AWS coreMQTT Backend
 *
 * Zero-allocation MQTT v5 client optimized for embedded use.
 * Requires manual event processing via process_events().
 *
 * Memory model:
 * - All buffers pre-allocated at construction
 * - No heap allocations during operation
 * - Deterministic memory footprint
 *
 * Threading model:
 * - Single-threaded, non-blocking
 * - Call process_events() from your event loop
 */
class CoreMQTTBackend : public IMQTTBackend {
public:
    /**
     * @brief Construct with default buffer configuration
     */
    CoreMQTTBackend();

    /**
     * @brief Construct with custom buffer sizes
     */
    explicit CoreMQTTBackend(size_t network_buffer_size,
                             size_t max_subscriptions = 64);

    ~CoreMQTTBackend() override;

    // Non-copyable, non-movable
    CoreMQTTBackend(const CoreMQTTBackend&) = delete;
    CoreMQTTBackend& operator=(const CoreMQTTBackend&) = delete;

    //=========================================================================
    // IMQTTBackend Implementation
    //=========================================================================

    BackendType type() const noexcept override { return BackendType::COREMQTT; }
    std::string_view name() const noexcept override { return "AWS coreMQTT"; }
    std::string_view version() const noexcept override { return "2.1.1"; }

    bool initialize(const ConnectionConfig& config) override;
    bool connect() override;
    void disconnect(uint32_t timeout_ms = 5000) override;
    bool is_connected() const noexcept override;
    ConnectionState state() const noexcept override;
    std::string_view client_id() const noexcept override;

    uint16_t publish(
        std::string_view topic,
        std::span<const uint8_t> payload,
        QoS qos = QoS::AT_LEAST_ONCE,
        bool retained = false
    ) override;

    bool publish_sync(
        std::string_view topic,
        std::span<const uint8_t> payload,
        QoS qos,
        bool retained,
        uint32_t timeout_ms
    ) override;

    bool subscribe(std::string_view topic, QoS qos = QoS::AT_LEAST_ONCE) override;
    bool unsubscribe(std::string_view topic) override;

    void set_connection_callback(ConnectionCallback cb) override;
    void set_message_callback(MessageCallback cb) override;
    void set_delivery_callback(DeliveryCallback cb) override;

    // coreMQTT requires manual event processing
    bool requires_event_loop() const noexcept override { return true; }
    int process_events(uint32_t timeout_ms = 0) override;

    const BackendStats& stats() const noexcept override { return stats_; }
    void reset_stats() noexcept override { stats_.reset(); }

    size_t static_memory_usage() const noexcept override;
    size_t dynamic_memory_usage() const noexcept override { return 0; }  // No dynamic allocs

    //=========================================================================
    // coreMQTT Specific
    //=========================================================================

    /**
     * @brief Get time since last activity (for keep-alive management)
     */
    std::chrono::milliseconds time_since_last_activity() const noexcept;

    /**
     * @brief Check if keep-alive ping is needed
     */
    bool needs_ping() const noexcept;

    /**
     * @brief Send keep-alive ping
     */
    bool send_ping();

private:
    // Implementation details
    class Impl;
    std::unique_ptr<Impl> impl_;

    // Callbacks
    ConnectionCallback connection_cb_;
    MessageCallback message_cb_;
    DeliveryCallback delivery_cb_;
    mutable std::mutex callback_mutex_;

    // State
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED};
    std::string client_id_;

    // Statistics
    mutable BackendStats stats_;

    // Internal methods
    void notify_connection_state(ConnectionState new_state, std::string_view reason);
    void on_incoming_publish(const char* topic, size_t topic_len,
                            const uint8_t* payload, size_t payload_len,
                            uint8_t qos, bool retained);
    void on_ack_received(uint16_t packet_id, bool success);
};

} // namespace ipb::transport::mqtt
