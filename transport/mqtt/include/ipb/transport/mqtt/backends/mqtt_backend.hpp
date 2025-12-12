#pragma once

/**
 * @file mqtt_backend.hpp
 * @brief Abstract MQTT backend interface
 *
 * Defines the interface that all MQTT backends must implement.
 * This allows IPB to support multiple MQTT implementations:
 * - Paho MQTT (default, general purpose)
 * - coreMQTT (embedded, zero-allocation)
 * - Native IPB (future, ultra-low latency)
 */

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <functional>
#include <chrono>
#include <memory>
#include <span>
#include <optional>

namespace ipb::transport::mqtt {

//=============================================================================
// Forward declarations
//=============================================================================

struct ConnectionConfig;
struct TLSConfig;

//=============================================================================
// Backend Types
//=============================================================================

/**
 * @brief Available MQTT backend implementations
 */
enum class BackendType {
    PAHO,       ///< Eclipse Paho MQTT (default)
    COREMQTT,   ///< AWS coreMQTT (embedded)
    NATIVE      ///< Native IPB implementation (future)
};

/**
 * @brief Get backend type name
 */
constexpr std::string_view backend_type_name(BackendType type) noexcept {
    switch (type) {
        case BackendType::PAHO: return "paho";
        case BackendType::COREMQTT: return "coremqtt";
        case BackendType::NATIVE: return "native";
        default: return "unknown";
    }
}

//=============================================================================
// QoS and Connection State (shared across backends)
//=============================================================================

/**
 * @brief MQTT Quality of Service levels
 */
enum class QoS : uint8_t {
    AT_MOST_ONCE = 0,   ///< Fire and forget
    AT_LEAST_ONCE = 1,  ///< Acknowledged delivery
    EXACTLY_ONCE = 2    ///< Assured delivery (4-way handshake)
};

/**
 * @brief Connection state
 */
enum class ConnectionState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    FAILED
};

/**
 * @brief Security mode
 */
enum class SecurityMode : uint8_t {
    NONE,               ///< Plain TCP
    TLS,                ///< TLS encryption
    TLS_PSK,            ///< TLS with Pre-Shared Key
    TLS_CLIENT_CERT     ///< TLS with client certificate
};

//=============================================================================
// Callbacks
//=============================================================================

/**
 * @brief Connection state change callback
 */
using ConnectionCallback = std::function<void(ConnectionState state, std::string_view reason)>;

/**
 * @brief Message received callback
 * @param topic MQTT topic
 * @param payload Message payload (view valid only during callback)
 * @param qos Message QoS
 * @param retained Was message retained
 */
using MessageCallback = std::function<void(
    std::string_view topic,
    std::span<const uint8_t> payload,
    QoS qos,
    bool retained
)>;

/**
 * @brief Delivery complete callback
 * @param token Message token/ID
 * @param success True if delivery confirmed
 */
using DeliveryCallback = std::function<void(uint16_t token, bool success)>;

//=============================================================================
// Backend Statistics
//=============================================================================

/**
 * @brief Backend statistics (zero-overhead when not used)
 */
struct BackendStats {
    uint64_t messages_sent = 0;
    uint64_t messages_received = 0;
    uint64_t messages_failed = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t reconnect_count = 0;

    // Latency tracking (optional)
    uint64_t total_publish_time_ns = 0;
    uint64_t publish_count = 0;

    constexpr uint64_t avg_publish_time_ns() const noexcept {
        return publish_count > 0 ? total_publish_time_ns / publish_count : 0;
    }

    constexpr void reset() noexcept {
        messages_sent = 0;
        messages_received = 0;
        messages_failed = 0;
        bytes_sent = 0;
        bytes_received = 0;
        reconnect_count = 0;
        total_publish_time_ns = 0;
        publish_count = 0;
    }
};

//=============================================================================
// IMQTTBackend Interface
//=============================================================================

/**
 * @brief Abstract MQTT backend interface
 *
 * All MQTT implementations must implement this interface.
 * Designed for embedded use:
 * - No exceptions in hot path
 * - Zero-copy message handling where possible
 * - Minimal allocations
 */
class IMQTTBackend {
public:
    virtual ~IMQTTBackend() = default;

    //=========================================================================
    // Backend Info
    //=========================================================================

    /**
     * @brief Get backend type
     */
    virtual BackendType type() const noexcept = 0;

    /**
     * @brief Get backend name
     */
    virtual std::string_view name() const noexcept = 0;

    /**
     * @brief Get backend version string
     */
    virtual std::string_view version() const noexcept = 0;

    //=========================================================================
    // Connection Management
    //=========================================================================

    /**
     * @brief Initialize backend with configuration
     * @param config Connection configuration
     * @return true on success
     */
    virtual bool initialize(const ConnectionConfig& config) = 0;

    /**
     * @brief Connect to MQTT broker
     * @return true if connection initiated
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnect from broker
     * @param timeout_ms Maximum wait time for clean disconnect
     */
    virtual void disconnect(uint32_t timeout_ms = 5000) = 0;

    /**
     * @brief Check if connected
     */
    virtual bool is_connected() const noexcept = 0;

    /**
     * @brief Get current connection state
     */
    virtual ConnectionState state() const noexcept = 0;

    /**
     * @brief Get client ID being used
     */
    virtual std::string_view client_id() const noexcept = 0;

    //=========================================================================
    // Publishing
    //=========================================================================

    /**
     * @brief Publish message (async)
     * @param topic MQTT topic
     * @param payload Message payload
     * @param qos Quality of service
     * @param retained Retain flag
     * @return Message token (>0) on success, 0 on failure
     */
    virtual uint16_t publish(
        std::string_view topic,
        std::span<const uint8_t> payload,
        QoS qos = QoS::AT_LEAST_ONCE,
        bool retained = false
    ) = 0;

    /**
     * @brief Publish message (sync with timeout)
     * @return true if delivery confirmed within timeout
     */
    virtual bool publish_sync(
        std::string_view topic,
        std::span<const uint8_t> payload,
        QoS qos,
        bool retained,
        uint32_t timeout_ms
    ) = 0;

    //=========================================================================
    // Subscribing
    //=========================================================================

    /**
     * @brief Subscribe to topic
     * @param topic Topic filter (supports wildcards)
     * @param qos Maximum QoS for received messages
     * @return true if subscription initiated
     */
    virtual bool subscribe(std::string_view topic, QoS qos = QoS::AT_LEAST_ONCE) = 0;

    /**
     * @brief Unsubscribe from topic
     */
    virtual bool unsubscribe(std::string_view topic) = 0;

    //=========================================================================
    // Callbacks
    //=========================================================================

    /**
     * @brief Set connection state callback
     */
    virtual void set_connection_callback(ConnectionCallback cb) = 0;

    /**
     * @brief Set message received callback
     */
    virtual void set_message_callback(MessageCallback cb) = 0;

    /**
     * @brief Set delivery complete callback
     */
    virtual void set_delivery_callback(DeliveryCallback cb) = 0;

    //=========================================================================
    // Event Processing (for single-threaded backends)
    //=========================================================================

    /**
     * @brief Process pending I/O events
     *
     * For backends that don't use internal threads (like coreMQTT),
     * this must be called regularly to process incoming/outgoing data.
     *
     * @param timeout_ms Maximum time to block waiting for events
     * @return Number of events processed
     */
    virtual int process_events(uint32_t timeout_ms = 0) {
        // Default implementation for threaded backends
        (void)timeout_ms;
        return 0;
    }

    /**
     * @brief Check if backend requires manual event processing
     */
    virtual bool requires_event_loop() const noexcept {
        return false;  // Most backends handle this internally
    }

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get backend statistics
     */
    virtual const BackendStats& stats() const noexcept = 0;

    /**
     * @brief Reset statistics
     */
    virtual void reset_stats() noexcept = 0;

    //=========================================================================
    // Memory Info (for embedded monitoring)
    //=========================================================================

    /**
     * @brief Get static memory usage (bytes)
     */
    virtual size_t static_memory_usage() const noexcept { return 0; }

    /**
     * @brief Get dynamic memory usage (bytes)
     */
    virtual size_t dynamic_memory_usage() const noexcept { return 0; }
};

//=============================================================================
// Backend Factory
//=============================================================================

/**
 * @brief Create MQTT backend instance
 * @param type Backend type to create
 * @return Unique pointer to backend, or nullptr if type not available
 */
std::unique_ptr<IMQTTBackend> create_backend(BackendType type);

/**
 * @brief Get default backend type (based on compile-time configuration)
 */
BackendType default_backend_type() noexcept;

/**
 * @brief Check if backend type is available
 */
bool is_backend_available(BackendType type) noexcept;

} // namespace ipb::transport::mqtt
