#pragma once

/**
 * @file protocol_capabilities.hpp
 * @brief Protocol capabilities and metadata definitions
 *
 * Provides comprehensive metadata about protocol implementations including:
 * - Security and authentication capabilities
 * - Performance characteristics
 * - Platform support
 * - ISO/OSI layer intervention levels
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ipb::common {

// ============================================================================
// ENUMERATIONS
// ============================================================================

/**
 * @brief Protocol communication mode
 */
enum class CommunicationMode : uint8_t {
    SYNCHRONOUS,      ///< Request-response, blocking
    ASYNCHRONOUS,     ///< Non-blocking, callback-based
    REAL_TIME,        ///< Hard real-time constraints
    NEAR_REAL_TIME,   ///< Soft real-time, low latency
    BATCH,            ///< Bulk data transfer
    STREAMING         ///< Continuous data flow
};

/**
 * @brief Security protocol support
 */
enum class SecurityProtocol : uint8_t {
    NONE = 0,
    TLS_1_2 = 1,
    TLS_1_3 = 2,
    DTLS = 3,
    SSH = 4,
    IPSEC = 5,
    CUSTOM = 255
};

/**
 * @brief Authentication mechanism
 */
enum class AuthMechanism : uint8_t {
    NONE = 0,
    USERNAME_PASSWORD,
    CERTIFICATE_X509,
    TOKEN_JWT,
    TOKEN_OAUTH2,
    KERBEROS,
    LDAP,
    SAML,
    API_KEY,
    MUTUAL_TLS,
    CUSTOM = 255
};

/**
 * @brief Authorization model
 */
enum class AuthorizationModel : uint8_t {
    NONE = 0,
    RBAC,             ///< Role-Based Access Control
    ABAC,             ///< Attribute-Based Access Control
    ACL,              ///< Access Control Lists
    CAPABILITY_BASED,
    CUSTOM = 255
};

/**
 * @brief Target deployment platform
 */
enum class DeploymentPlatform : uint8_t {
    EMBEDDED_BARE_METAL = 0,  ///< No OS, direct hardware
    EMBEDDED_RTOS,            ///< FreeRTOS, Zephyr, etc.
    EMBEDDED_LINUX,           ///< Yocto, Buildroot
    EDGE_GATEWAY,             ///< Edge computing devices
    EDGE_MOBILE,              ///< Smartphones, tablets
    SERVER_STANDARD,          ///< Standard server deployment
    SERVER_CLOUD,             ///< Cloud-native deployment
    SERVER_CONTAINERIZED      ///< Docker, Kubernetes
};

/**
 * @brief ISO/OSI layer intervention
 */
enum class ISOLayer : uint8_t {
    PHYSICAL = 1,      ///< Layer 1 - Physical
    DATA_LINK = 2,     ///< Layer 2 - Data Link
    NETWORK = 3,       ///< Layer 3 - Network
    TRANSPORT = 4,     ///< Layer 4 - Transport
    SESSION = 5,       ///< Layer 5 - Session
    PRESENTATION = 6,  ///< Layer 6 - Presentation
    APPLICATION = 7    ///< Layer 7 - Application
};

/**
 * @brief Protocol type classification
 */
enum class ProtocolType : uint8_t {
    // Industrial protocols
    MODBUS_RTU,
    MODBUS_TCP,
    MODBUS_ASCII,
    OPCUA,
    PROFINET,
    PROFIBUS,
    ETHERCAT,
    CANOPEN,
    DEVICENET,
    BACNET,
    HART,
    FOUNDATION_FIELDBUS,

    // IoT protocols
    MQTT,
    MQTT_SN,          ///< MQTT for Sensor Networks
    COAP,
    AMQP,
    DDS,
    SPARKPLUG_B,
    LWM2M,

    // IT protocols
    HTTP,
    HTTPS,
    WEBSOCKET,
    GRPC,
    REST,
    GRAPHQL,

    // Messaging
    KAFKA,
    RABBITMQ,
    ZEROMQ,
    REDIS_PUBSUB,

    // Database
    INFLUXDB,
    TIMESCALEDB,
    MONGODB,

    // Custom
    CUSTOM = 255
};

// ============================================================================
// RESOURCE REQUIREMENTS
// ============================================================================

/**
 * @brief Memory requirements specification
 */
struct MemoryRequirements {
    uint64_t min_ram_bytes = 0;         ///< Minimum RAM required
    uint64_t recommended_ram_bytes = 0;  ///< Recommended RAM
    uint64_t max_ram_bytes = 0;          ///< Maximum RAM usage
    uint64_t min_flash_bytes = 0;        ///< Minimum flash/storage
    uint64_t stack_size_bytes = 0;       ///< Per-thread stack size
    bool uses_heap = true;               ///< Whether heap allocation is used
    bool zero_allocation_mode = false;   ///< Supports zero-alloc operation
};

/**
 * @brief CPU requirements specification
 */
struct CpuRequirements {
    uint32_t min_frequency_mhz = 0;      ///< Minimum CPU frequency
    uint32_t recommended_frequency_mhz = 0;
    uint8_t min_cores = 1;               ///< Minimum CPU cores
    uint8_t recommended_cores = 1;
    bool requires_fpu = false;           ///< Floating point unit required
    bool requires_simd = false;          ///< SIMD instructions required
    std::vector<std::string> supported_architectures;  ///< arm, x86, riscv, etc.
};

/**
 * @brief Network requirements specification
 */
struct NetworkRequirements {
    uint32_t min_bandwidth_kbps = 0;     ///< Minimum bandwidth
    uint32_t recommended_bandwidth_kbps = 0;
    uint32_t max_latency_ms = 0;         ///< Maximum tolerable latency
    bool requires_multicast = false;
    bool requires_broadcast = false;
    bool ipv4_supported = true;
    bool ipv6_supported = false;
    std::vector<uint16_t> default_ports;
};

/**
 * @brief Platform profile with all requirements
 */
struct PlatformProfile {
    DeploymentPlatform platform;
    std::string name;
    std::string description;

    MemoryRequirements memory;
    CpuRequirements cpu;
    NetworkRequirements network;

    bool is_supported = false;
    std::string notes;
};

// ============================================================================
// LATENCY CHARACTERISTICS
// ============================================================================

/**
 * @brief Latency statistics and guarantees
 */
struct LatencyCharacteristics {
    // Typical latencies
    std::chrono::microseconds typical_latency{0};
    std::chrono::microseconds min_latency{0};
    std::chrono::microseconds max_latency{0};
    std::chrono::microseconds p99_latency{0};      ///< 99th percentile
    std::chrono::microseconds p999_latency{0};     ///< 99.9th percentile

    // Jitter
    std::chrono::microseconds typical_jitter{0};
    std::chrono::microseconds max_jitter{0};

    // Throughput
    uint32_t max_messages_per_second = 0;
    uint64_t max_bytes_per_second = 0;

    // Real-time characteristics
    bool deterministic = false;           ///< Deterministic timing
    bool hard_real_time = false;          ///< Hard RT guarantees
    std::chrono::microseconds cycle_time{0};  ///< For cyclic protocols
};

// ============================================================================
// SECURITY CAPABILITIES
// ============================================================================

/**
 * @brief Application-level authentication
 */
struct AppAuthentication {
    bool supported = false;
    std::vector<AuthMechanism> mechanisms;
    bool multi_factor_supported = false;
    bool session_management = false;
    std::chrono::seconds session_timeout{0};
    uint32_t max_sessions = 0;
};

/**
 * @brief User-level authentication
 */
struct UserAuthentication {
    bool supported = false;
    std::vector<AuthMechanism> mechanisms;
    bool multi_factor_supported = false;
    bool password_policy_enforced = false;
    bool account_lockout_supported = false;
    uint32_t max_failed_attempts = 0;
    std::chrono::seconds lockout_duration{0};
};

/**
 * @brief Authorization capabilities
 */
struct AuthorizationCapabilities {
    bool supported = false;
    AuthorizationModel model = AuthorizationModel::NONE;
    bool fine_grained = false;           ///< Resource-level permissions
    bool hierarchical = false;           ///< Role hierarchy support
    bool dynamic_policies = false;       ///< Runtime policy updates
    std::vector<std::string> built_in_roles;
};

/**
 * @brief Complete security capabilities
 */
struct SecurityCapabilities {
    // Transport security
    bool transport_encryption = false;
    std::vector<SecurityProtocol> supported_protocols;
    SecurityProtocol default_protocol = SecurityProtocol::NONE;

    // Certificate support
    bool certificate_validation = false;
    bool certificate_revocation_check = false;
    bool mutual_authentication = false;

    // Authentication
    AppAuthentication app_auth;
    UserAuthentication user_auth;

    // Authorization
    AuthorizationCapabilities authorization;

    // Data protection
    bool payload_encryption = false;
    bool message_signing = false;
    bool integrity_check = false;
    bool replay_protection = false;

    // Audit
    bool audit_logging = false;
    bool security_events = false;
};

// ============================================================================
// PROTOCOL CAPABILITIES
// ============================================================================

/**
 * @brief Complete protocol capabilities specification
 */
struct ProtocolCapabilities {
    // Identity
    std::string protocol_name;
    std::string protocol_version;
    ProtocolType type = ProtocolType::CUSTOM;
    std::string vendor;
    std::string specification_url;

    // Classification
    std::vector<CommunicationMode> supported_modes;
    CommunicationMode default_mode = CommunicationMode::SYNCHRONOUS;

    // ISO/OSI layers
    std::vector<ISOLayer> intervention_layers;
    ISOLayer primary_layer = ISOLayer::APPLICATION;

    // Platform support
    std::vector<PlatformProfile> platform_profiles;

    // Performance
    LatencyCharacteristics latency;

    // Security
    SecurityCapabilities security;

    // Features
    bool supports_discovery = false;
    bool supports_auto_reconnect = false;
    bool supports_qos = false;
    bool supports_compression = false;
    bool supports_batching = false;
    bool supports_transactions = false;
    bool supports_subscriptions = false;
    bool bidirectional = false;

    // Data characteristics
    uint32_t max_payload_bytes = 0;
    uint32_t max_topic_length = 0;
    bool binary_payload = true;
    bool text_payload = true;

    // Reliability
    bool at_most_once = false;     ///< QoS 0
    bool at_least_once = false;    ///< QoS 1
    bool exactly_once = false;     ///< QoS 2
    bool ordered_delivery = false;

    // Helper methods
    bool supports_platform(DeploymentPlatform platform) const noexcept {
        for (const auto& profile : platform_profiles) {
            if (profile.platform == platform && profile.is_supported) {
                return true;
            }
        }
        return false;
    }

    bool supports_security() const noexcept {
        return security.transport_encryption ||
               security.payload_encryption ||
               security.app_auth.supported ||
               security.user_auth.supported;
    }

    bool supports_real_time() const noexcept {
        for (auto mode : supported_modes) {
            if (mode == CommunicationMode::REAL_TIME ||
                mode == CommunicationMode::NEAR_REAL_TIME) {
                return true;
            }
        }
        return false;
    }

    std::optional<PlatformProfile> get_profile(DeploymentPlatform platform) const noexcept {
        for (const auto& profile : platform_profiles) {
            if (profile.platform == platform) {
                return profile;
            }
        }
        return std::nullopt;
    }
};

// ============================================================================
// PROTOCOL INFO FOR SCOOPS/SINKS
// ============================================================================

/**
 * @brief Protocol information for Scoop/Sink implementations
 *
 * This is attached to ScoopInfo and SinkInfo to provide
 * detailed protocol metadata.
 */
struct ProtocolInfo {
    // Basic identity
    ProtocolType type = ProtocolType::CUSTOM;
    std::string name;
    std::string version;

    // Full capabilities (optional, for detailed introspection)
    std::optional<ProtocolCapabilities> capabilities;

    // Quick access flags (computed from capabilities if available)
    struct {
        bool secure = false;
        bool authenticated = false;
        bool real_time = false;
        bool reliable = false;
        bool bidirectional = false;
    } flags;

    // Current configuration
    struct {
        SecurityProtocol security_protocol = SecurityProtocol::NONE;
        AuthMechanism auth_mechanism = AuthMechanism::NONE;
        CommunicationMode comm_mode = CommunicationMode::ASYNCHRONOUS;
        uint8_t qos_level = 0;
    } current_config;

    // Runtime metrics
    struct {
        std::chrono::microseconds current_latency{0};
        uint64_t messages_per_second = 0;
        double availability_percent = 100.0;
    } metrics;
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Convert ProtocolType to string
 */
constexpr std::string_view protocol_type_to_string(ProtocolType type) noexcept {
    switch (type) {
        case ProtocolType::MODBUS_RTU: return "Modbus RTU";
        case ProtocolType::MODBUS_TCP: return "Modbus TCP";
        case ProtocolType::MODBUS_ASCII: return "Modbus ASCII";
        case ProtocolType::OPCUA: return "OPC UA";
        case ProtocolType::PROFINET: return "PROFINET";
        case ProtocolType::PROFIBUS: return "PROFIBUS";
        case ProtocolType::ETHERCAT: return "EtherCAT";
        case ProtocolType::CANOPEN: return "CANopen";
        case ProtocolType::DEVICENET: return "DeviceNet";
        case ProtocolType::BACNET: return "BACnet";
        case ProtocolType::HART: return "HART";
        case ProtocolType::FOUNDATION_FIELDBUS: return "Foundation Fieldbus";
        case ProtocolType::MQTT: return "MQTT";
        case ProtocolType::MQTT_SN: return "MQTT-SN";
        case ProtocolType::COAP: return "CoAP";
        case ProtocolType::AMQP: return "AMQP";
        case ProtocolType::DDS: return "DDS";
        case ProtocolType::SPARKPLUG_B: return "Sparkplug B";
        case ProtocolType::LWM2M: return "LwM2M";
        case ProtocolType::HTTP: return "HTTP";
        case ProtocolType::HTTPS: return "HTTPS";
        case ProtocolType::WEBSOCKET: return "WebSocket";
        case ProtocolType::GRPC: return "gRPC";
        case ProtocolType::REST: return "REST";
        case ProtocolType::GRAPHQL: return "GraphQL";
        case ProtocolType::KAFKA: return "Kafka";
        case ProtocolType::RABBITMQ: return "RabbitMQ";
        case ProtocolType::ZEROMQ: return "ZeroMQ";
        case ProtocolType::REDIS_PUBSUB: return "Redis Pub/Sub";
        case ProtocolType::INFLUXDB: return "InfluxDB";
        case ProtocolType::TIMESCALEDB: return "TimescaleDB";
        case ProtocolType::MONGODB: return "MongoDB";
        case ProtocolType::CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

/**
 * @brief Convert DeploymentPlatform to string
 */
constexpr std::string_view deployment_platform_to_string(DeploymentPlatform platform) noexcept {
    switch (platform) {
        case DeploymentPlatform::EMBEDDED_BARE_METAL: return "Bare Metal";
        case DeploymentPlatform::EMBEDDED_RTOS: return "RTOS";
        case DeploymentPlatform::EMBEDDED_LINUX: return "Embedded Linux";
        case DeploymentPlatform::EDGE_GATEWAY: return "Edge Gateway";
        case DeploymentPlatform::EDGE_MOBILE: return "Mobile Edge";
        case DeploymentPlatform::SERVER_STANDARD: return "Server";
        case DeploymentPlatform::SERVER_CLOUD: return "Cloud";
        case DeploymentPlatform::SERVER_CONTAINERIZED: return "Container";
        default: return "Unknown";
    }
}

} // namespace ipb::common
