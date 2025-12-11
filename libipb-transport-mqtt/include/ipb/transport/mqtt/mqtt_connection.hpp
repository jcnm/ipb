#pragma once

/**
 * @file mqtt_connection.hpp
 * @brief Shared MQTT transport layer for IPB
 *
 * This library provides a shared MQTT client connection that can be used by
 * multiple sinks and scoops (MQTT generic, Sparkplug B, etc.) to avoid
 * duplicating the MQTT client library across components.
 *
 * Architecture:
 * - MQTTConnectionManager: Singleton managing shared connections
 * - MQTTConnection: Individual connection wrapper (backend-agnostic)
 * - IMQTTBackend: Abstract backend interface (Paho, coreMQTT, Native)
 *
 * Backend selection:
 * - Compile time: -DIPB_MQTT_DEFAULT_COREMQTT=1
 * - Runtime: ConnectionConfig::backend field
 */

#include "backends/mqtt_backend.hpp"

#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <vector>
#include <optional>

namespace ipb::transport::mqtt {

// Note: QoS, SecurityMode, ConnectionState, BackendType are defined in backends/mqtt_backend.hpp

//=============================================================================
// Shared Configurations
//=============================================================================

/**
 * @brief TLS/SSL configuration
 */
struct TLSConfig {
    std::string ca_cert_path;           ///< CA certificate file path
    std::string client_cert_path;       ///< Client certificate file path
    std::string client_key_path;        ///< Client private key file path
    std::string psk_identity;           ///< PSK identity (for TLS_PSK)
    std::string psk_key;                ///< PSK key (for TLS_PSK)
    bool verify_hostname = true;        ///< Verify server hostname
    bool verify_certificate = true;     ///< Verify server certificate
    bool verify_server = true;          ///< Verify server (alias for verify_certificate)
    std::vector<std::string> alpn_protocols;  ///< ALPN protocols
};

/**
 * @brief Last Will and Testament configuration
 */
struct LWTConfig {
    bool enabled = false;
    std::string topic;
    std::string payload;
    QoS qos = QoS::AT_LEAST_ONCE;
    bool retained = false;
};

/**
 * @brief MQTT connection configuration (shared by all MQTT components)
 */
struct ConnectionConfig {
    // Backend selection
    BackendType backend = default_backend_type();  ///< Which backend to use

    // Broker settings
    std::string broker_url = "tcp://localhost:1883";
    std::string client_id;              ///< Empty = auto-generated

    // Authentication
    std::string username;
    std::string password;

    // Connection parameters
    std::chrono::seconds keep_alive{60};
    uint16_t keep_alive_seconds = 60;   ///< Keep-alive in seconds (for backends)
    std::chrono::seconds connect_timeout{30};
    bool clean_session = true;

    // Reconnection
    bool auto_reconnect = true;
    std::chrono::seconds min_reconnect_delay{1};
    std::chrono::seconds max_reconnect_delay{60};
    uint32_t reconnect_delay_seconds = 5;  ///< For backends
    int max_reconnect_attempts = -1;    ///< -1 = infinite

    // Security
    SecurityMode security = SecurityMode::NONE;
    TLSConfig tls;

    // Last Will and Testament
    LWTConfig lwt;
    std::string lwt_topic;              ///< For backends
    std::string lwt_payload;            ///< For backends
    QoS lwt_qos = QoS::AT_LEAST_ONCE;   ///< For backends
    bool lwt_retained = false;          ///< For backends

    // Performance
    size_t max_inflight = 100;          ///< Max in-flight messages
    size_t max_buffered = 10000;        ///< Max buffered messages when disconnected

    // Sync LWT fields from LWTConfig
    void sync_lwt() {
        if (lwt.enabled) {
            lwt_topic = lwt.topic;
            lwt_payload = lwt.payload;
            lwt_qos = lwt.qos;
            lwt_retained = lwt.retained;
        }
    }

    // Validation
    bool is_valid() const noexcept;
    std::string validation_error() const;
};

//=============================================================================
// Callbacks
//=============================================================================

using ConnectionCallback = std::function<void(ConnectionState state, const std::string& reason)>;
using MessageCallback = std::function<void(const std::string& topic, const std::string& payload, QoS qos, bool retained)>;
using DeliveryCallback = std::function<void(int token, bool success, const std::string& error)>;

//=============================================================================
// MQTTConnection - Individual connection wrapper
//=============================================================================

/**
 * @brief MQTT Connection wrapper
 *
 * Provides a high-level, backend-agnostic interface with:
 * - Multiple backend support (Paho, coreMQTT, Native)
 * - Automatic reconnection
 * - Thread-safe operations
 * - Callback-based message handling
 * - Statistics collection
 *
 * Backend selection via ConnectionConfig::backend or compile-time default.
 */
class MQTTConnection {
public:
    /**
     * @brief Construct a new MQTT Connection
     * @param config Connection configuration
     */
    explicit MQTTConnection(const ConnectionConfig& config);

    /**
     * @brief Destructor - ensures clean disconnection
     */
    ~MQTTConnection();

    // Non-copyable, movable
    MQTTConnection(const MQTTConnection&) = delete;
    MQTTConnection& operator=(const MQTTConnection&) = delete;
    MQTTConnection(MQTTConnection&&) noexcept;
    MQTTConnection& operator=(MQTTConnection&&) noexcept;

    //=========================================================================
    // Connection Management
    //=========================================================================

    /**
     * @brief Connect to the MQTT broker
     * @return true if connection initiated successfully
     */
    bool connect();

    /**
     * @brief Disconnect from the MQTT broker
     * @param timeout Maximum time to wait for clean disconnection
     */
    void disconnect(std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

    /**
     * @brief Check if connected
     */
    bool is_connected() const noexcept;

    /**
     * @brief Get current connection state
     */
    ConnectionState get_state() const noexcept;

    /**
     * @brief Get the client ID being used
     */
    std::string get_client_id() const;

    /**
     * @brief Get the backend type being used
     */
    BackendType get_backend_type() const noexcept;

    /**
     * @brief Get the underlying backend (for advanced use)
     */
    IMQTTBackend* get_backend() noexcept;

    /**
     * @brief Process events (required for non-threaded backends like coreMQTT)
     * @param timeout_ms Maximum time to block
     * @return Number of events processed, -1 on error
     */
    int process_events(uint32_t timeout_ms = 0);

    /**
     * @brief Check if backend requires manual event processing
     */
    bool requires_event_loop() const noexcept;

    //=========================================================================
    // Publishing
    //=========================================================================

    /**
     * @brief Publish a message
     * @param topic MQTT topic
     * @param payload Message payload
     * @param qos Quality of Service
     * @param retained Retain flag
     * @return Delivery token (for tracking)
     */
    int publish(const std::string& topic,
                const std::string& payload,
                QoS qos = QoS::AT_LEAST_ONCE,
                bool retained = false);

    /**
     * @brief Publish a message (binary payload)
     */
    int publish(const std::string& topic,
                const std::vector<uint8_t>& payload,
                QoS qos = QoS::AT_LEAST_ONCE,
                bool retained = false);

    /**
     * @brief Publish and wait for completion
     * @return true if delivery confirmed
     */
    bool publish_sync(const std::string& topic,
                      const std::string& payload,
                      QoS qos = QoS::AT_LEAST_ONCE,
                      bool retained = false,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});

    //=========================================================================
    // Subscribing
    //=========================================================================

    /**
     * @brief Subscribe to a topic
     * @param topic Topic filter (supports wildcards)
     * @param qos Maximum QoS for received messages
     * @return true if subscription initiated
     */
    bool subscribe(const std::string& topic, QoS qos = QoS::AT_LEAST_ONCE);

    /**
     * @brief Subscribe to multiple topics
     */
    bool subscribe(const std::vector<std::pair<std::string, QoS>>& topics);

    /**
     * @brief Unsubscribe from a topic
     */
    bool unsubscribe(const std::string& topic);

    /**
     * @brief Unsubscribe from multiple topics
     */
    bool unsubscribe(const std::vector<std::string>& topics);

    //=========================================================================
    // Callbacks
    //=========================================================================

    /**
     * @brief Set connection state callback
     */
    void set_connection_callback(ConnectionCallback cb);

    /**
     * @brief Set message received callback
     */
    void set_message_callback(MessageCallback cb);

    /**
     * @brief Set delivery complete callback
     */
    void set_delivery_callback(DeliveryCallback cb);

    //=========================================================================
    // Statistics
    //=========================================================================

    struct Statistics {
        std::atomic<uint64_t> messages_published{0};
        std::atomic<uint64_t> messages_received{0};
        std::atomic<uint64_t> messages_failed{0};
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<uint64_t> reconnect_count{0};
        std::chrono::steady_clock::time_point connected_since;

        void reset() {
            messages_published = 0;
            messages_received = 0;
            messages_failed = 0;
            bytes_sent = 0;
            bytes_received = 0;
            reconnect_count = 0;
        }
    };

    /**
     * @brief Get connection statistics
     */
    const Statistics& get_statistics() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// MQTTConnectionManager - Singleton for shared connections
//=============================================================================

/**
 * @brief Connection Manager for shared MQTT connections
 *
 * Allows multiple components (sinks/scoops) to share the same MQTT connection
 * to a broker, avoiding duplicate connections and resource usage.
 *
 * Usage:
 * @code
 * auto& manager = MQTTConnectionManager::instance();
 * auto conn = manager.get_or_create("broker1", config);
 * conn->publish("topic", "payload");
 * @endcode
 */
class MQTTConnectionManager {
public:
    /**
     * @brief Get the singleton instance
     */
    static MQTTConnectionManager& instance();

    /**
     * @brief Get or create a shared connection
     * @param connection_id Unique identifier for this connection
     * @param config Connection configuration (used only if creating new)
     * @return Shared pointer to the connection
     */
    std::shared_ptr<MQTTConnection> get_or_create(
        const std::string& connection_id,
        const ConnectionConfig& config);

    /**
     * @brief Get an existing connection
     * @param connection_id Connection identifier
     * @return Connection or nullptr if not found
     */
    std::shared_ptr<MQTTConnection> get(const std::string& connection_id);

    /**
     * @brief Check if a connection exists
     */
    bool has_connection(const std::string& connection_id) const;

    /**
     * @brief Remove a connection (if reference count allows)
     */
    void remove(const std::string& connection_id);

    /**
     * @brief Get all active connection IDs
     */
    std::vector<std::string> get_connection_ids() const;

    /**
     * @brief Get number of active connections
     */
    size_t connection_count() const;

    /**
     * @brief Disconnect all connections
     */
    void disconnect_all();

private:
    MQTTConnectionManager() = default;
    ~MQTTConnectionManager();

    MQTTConnectionManager(const MQTTConnectionManager&) = delete;
    MQTTConnectionManager& operator=(const MQTTConnectionManager&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<MQTTConnection>> connections_;
};

//=============================================================================
// Utility Functions
//=============================================================================

/**
 * @brief Generate a unique client ID
 * @param prefix Optional prefix
 * @return Unique client ID
 */
std::string generate_client_id(const std::string& prefix = "ipb");

/**
 * @brief Parse a broker URL
 * @param url Broker URL (e.g., "tcp://host:1883", "ssl://host:8883")
 * @return Tuple of (protocol, host, port) or nullopt on error
 */
std::optional<std::tuple<std::string, std::string, uint16_t>> parse_broker_url(const std::string& url);

/**
 * @brief Build a broker URL
 * @param host Broker hostname
 * @param port Broker port
 * @param use_tls Use TLS (ssl://) or plain TCP (tcp://)
 */
std::string build_broker_url(const std::string& host, uint16_t port, bool use_tls = false);

} // namespace ipb::transport::mqtt
