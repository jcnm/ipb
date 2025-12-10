#pragma once

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/endpoint.hpp"
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <chrono>
#include <queue>

namespace ipb::adapter::opcua {

/**
 * @brief OPC UA node class enumeration
 */
enum class NodeClass : uint8_t {
    OBJECT = 1,
    VARIABLE = 2,
    METHOD = 4,
    OBJECT_TYPE = 8,
    VARIABLE_TYPE = 16,
    REFERENCE_TYPE = 32,
    DATA_TYPE = 64,
    VIEW = 128
};

/**
 * @brief OPC UA access level
 */
enum class AccessLevel : uint8_t {
    NONE = 0,
    CURRENT_READ = 1,
    CURRENT_WRITE = 2,
    HISTORY_READ = 4,
    HISTORY_WRITE = 8,
    SEMANTIC_CHANGE = 16,
    STATUS_WRITE = 32,
    TIMESTAMP_WRITE = 64
};

/**
 * @brief OPC UA security policy
 */
enum class SecurityPolicy : uint8_t {
    NONE = 0,
    BASIC128RSA15,
    BASIC256,
    BASIC256SHA256,
    AES128_SHA256_RSAOAEP,
    AES256_SHA256_RSAPSS
};

/**
 * @brief OPC UA message security mode
 */
enum class MessageSecurityMode : uint8_t {
    NONE = 1,
    SIGN = 2,
    SIGN_AND_ENCRYPT = 3
};

/**
 * @brief OPC UA node identifier
 */
struct NodeId {
    uint16_t namespace_index = 0;
    std::string identifier;
    
    // Parse from string format: "ns=X;s=identifier" or "ns=X;i=numeric"
    static NodeId parse(std::string_view node_id_str);
    
    // Convert to string format
    std::string to_string() const;
    
    // Convert to UA_NodeId
    UA_NodeId to_ua_nodeid() const;
    
    // Validation
    bool is_valid() const noexcept;
    
    // Comparison
    bool operator==(const NodeId& other) const noexcept;
    size_t hash() const noexcept;
};

/**
 * @brief OPC UA subscription settings
 */
struct SubscriptionSettings {
    double publishing_interval = 100.0; // milliseconds
    uint32_t lifetime_count = 10000;
    uint32_t max_keepalive_count = 10;
    uint32_t max_notifications_per_publish = 1000;
    uint8_t priority = 0;
    bool publishing_enabled = true;
    
    // Monitored item settings
    double sampling_interval = 100.0; // milliseconds
    uint32_t queue_size = 10;
    bool discard_oldest = true;
};

/**
 * @brief OPC UA adapter configuration
 */
class OPCUAAdapterConfig : public ipb::common::ConfigurationBase {
public:
    // Connection settings
    std::string endpoint_url;
    std::chrono::milliseconds connection_timeout{10000};
    std::chrono::milliseconds session_timeout{60000};
    std::chrono::milliseconds request_timeout{5000};
    uint32_t max_retries = 3;
    std::chrono::milliseconds retry_delay{1000};
    
    // Security settings
    SecurityPolicy security_policy = SecurityPolicy::NONE;
    MessageSecurityMode security_mode = MessageSecurityMode::NONE;
    std::string username;
    std::string password;
    std::string certificate_path;
    std::string private_key_path;
    std::string trust_list_path;
    std::string revocation_list_path;
    
    // Application settings
    std::string application_name = "IPB OPC UA Client";
    std::string application_uri = "urn:ipb:opcua:client";
    std::string product_uri = "https://github.com/ipb/opcua-client";
    
    // Subscription settings
    SubscriptionSettings subscription;
    bool enable_subscriptions = true;
    uint32_t max_subscriptions = 10;
    
    // Performance settings
    uint32_t max_batch_size = 1000;
    std::chrono::milliseconds polling_interval{1000};
    bool enable_async_polling = true;
    uint32_t worker_thread_count = 2;
    
    // Real-time settings
    bool enable_realtime_priority = false;
    int realtime_priority = 50;
    int cpu_affinity = -1;
    
    // Data settings
    std::vector<NodeId> node_ids;
    bool enable_data_validation = true;
    bool enable_timestamp_server = true;
    bool enable_source_timestamp = true;
    
    // Error handling
    bool enable_error_recovery = true;
    uint32_t max_consecutive_errors = 10;
    std::chrono::milliseconds error_backoff_time{5000};
    bool enable_automatic_reconnection = true;
    
    // Monitoring
    bool enable_statistics = true;
    std::chrono::milliseconds statistics_interval{1000};
    bool enable_diagnostics = false;
    
    // ConfigurationBase interface
    ipb::common::Result<> validate() const override;
    std::string to_string() const override;
    ipb::common::Result<> from_string(std::string_view config) override;
    std::unique_ptr<ipb::common::ConfigurationBase> clone() const override;
    
    // Preset configurations
    static OPCUAAdapterConfig create_high_performance();
    static OPCUAAdapterConfig create_low_latency();
    static OPCUAAdapterConfig create_secure();
    static OPCUAAdapterConfig create_reliable();
};

/**
 * @brief High-performance OPC UA protocol adapter
 * 
 * Features:
 * - Full OPC UA client implementation
 * - Subscription-based real-time data monitoring
 * - Batch reading for optimal performance
 * - Security support (certificates, encryption)
 * - Automatic reconnection and error recovery
 * - Browse and discovery capabilities
 * - Method calling support
 * - Historical data access
 */
class OPCUAAdapter : public ipb::common::IProtocolSourceBase {
public:
    static constexpr uint16_t PROTOCOL_ID = 2;
    static constexpr std::string_view PROTOCOL_NAME = "OPC UA";
    static constexpr std::string_view COMPONENT_NAME = "OPCUAAdapter";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";
    
    OPCUAAdapter();
    ~OPCUAAdapter() override;
    
    // Disable copy/move for thread safety
    OPCUAAdapter(const OPCUAAdapter&) = delete;
    OPCUAAdapter& operator=(const OPCUAAdapter&) = delete;
    OPCUAAdapter(OPCUAAdapter&&) = delete;
    OPCUAAdapter& operator=(OPCUAAdapter&&) = delete;
    
    // IProtocolSourceBase interface
    ipb::common::Result<ipb::common::DataSet> read() override;
    ipb::common::Result<ipb::common::DataSet> read_async() override;
    
    ipb::common::Result<> subscribe(DataCallback data_cb, ErrorCallback error_cb) override;
    ipb::common::Result<> unsubscribe() override;
    
    ipb::common::Result<> add_address(std::string_view address) override;
    ipb::common::Result<> remove_address(std::string_view address) override;
    std::vector<std::string> get_addresses() const override;
    
    ipb::common::Result<> connect() override;
    ipb::common::Result<> disconnect() override;
    bool is_connected() const noexcept override;
    
    uint16_t protocol_id() const noexcept override { return PROTOCOL_ID; }
    std::string_view protocol_name() const noexcept override { return PROTOCOL_NAME; }
    
    // IIPBComponent interface
    ipb::common::Result<> start() override;
    ipb::common::Result<> stop() override;
    bool is_running() const noexcept override;
    
    ipb::common::Result<> configure(const ipb::common::ConfigurationBase& config) override;
    std::unique_ptr<ipb::common::ConfigurationBase> get_configuration() const override;
    
    ipb::common::Statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;
    
    bool is_healthy() const noexcept override;
    std::string get_health_status() const override;
    
    std::string_view component_name() const noexcept override { return COMPONENT_NAME; }
    std::string_view component_version() const noexcept override { return COMPONENT_VERSION; }
    
    // OPC UA specific methods
    ipb::common::Result<> write_value(const NodeId& node_id, const ipb::common::Value& value);
    ipb::common::Result<> write_values(const std::vector<std::pair<NodeId, ipb::common::Value>>& values);
    
    ipb::common::Result<ipb::common::Value> read_value(const NodeId& node_id);
    ipb::common::Result<std::vector<ipb::common::Value>> read_values(const std::vector<NodeId>& node_ids);
    
    // Browse and discovery
    ipb::common::Result<std::vector<NodeId>> browse_children(const NodeId& parent_node);
    ipb::common::Result<std::vector<NodeId>> browse_references(const NodeId& node_id);
    ipb::common::Result<std::string> read_display_name(const NodeId& node_id);
    ipb::common::Result<std::string> read_description(const NodeId& node_id);
    ipb::common::Result<NodeClass> read_node_class(const NodeId& node_id);
    ipb::common::Result<AccessLevel> read_access_level(const NodeId& node_id);
    
    // Method calling
    ipb::common::Result<std::vector<ipb::common::Value>> call_method(
        const NodeId& object_id, 
        const NodeId& method_id,
        const std::vector<ipb::common::Value>& input_args);
    
    // Historical data access
    ipb::common::Result<ipb::common::DataSet> read_historical_data(
        const NodeId& node_id,
        ipb::common::Timestamp start_time,
        ipb::common::Timestamp end_time,
        uint32_t max_values = 1000);
    
    // Subscription management
    ipb::common::Result<uint32_t> create_subscription(const SubscriptionSettings& settings);
    ipb::common::Result<> delete_subscription(uint32_t subscription_id);
    ipb::common::Result<uint32_t> add_monitored_item(uint32_t subscription_id, const NodeId& node_id);
    ipb::common::Result<> remove_monitored_item(uint32_t subscription_id, uint32_t monitored_item_id);
    
    // Server information
    ipb::common::Result<std::vector<std::string>> get_endpoints();
    ipb::common::Result<std::string> get_server_status();
    ipb::common::Result<std::chrono::system_clock::time_point> get_server_time();

private:
    // Configuration
    std::unique_ptr<OPCUAAdapterConfig> config_;
    
    // OPC UA client
    UA_Client* client_ = nullptr;
    UA_ClientConfig* client_config_ = nullptr;
    
    // State management
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> is_subscribed_{false};
    
    // Threading
    std::unique_ptr<std::thread> polling_thread_;
    std::unique_ptr<std::thread> subscription_thread_;
    std::unique_ptr<std::thread> statistics_thread_;
    mutable std::mutex state_mutex_;
    std::condition_variable stop_condition_;
    
    // Callbacks
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    
    // Node management
    std::vector<NodeId> node_ids_;
    mutable std::shared_mutex nodes_mutex_;
    
    // Subscription management
    struct SubscriptionInfo {
        uint32_t subscription_id;
        std::vector<uint32_t> monitored_items;
        SubscriptionSettings settings;
    };
    std::unordered_map<uint32_t, SubscriptionInfo> subscriptions_;
    std::atomic<uint32_t> next_subscription_id_{1};
    
    // Data queue for subscription callbacks
    struct DataNotification {
        NodeId node_id;
        ipb::common::Value value;
        ipb::common::Timestamp timestamp;
        ipb::common::Quality quality;
    };
    std::queue<DataNotification> data_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    
    // Statistics (lock-free)
    mutable std::atomic<uint64_t> total_reads_{0};
    mutable std::atomic<uint64_t> successful_reads_{0};
    mutable std::atomic<uint64_t> failed_reads_{0};
    mutable std::atomic<uint64_t> total_writes_{0};
    mutable std::atomic<uint64_t> successful_writes_{0};
    mutable std::atomic<uint64_t> failed_writes_{0};
    mutable std::atomic<uint64_t> total_subscriptions_{0};
    mutable std::atomic<uint64_t> total_notifications_{0};
    
    // Error tracking
    std::atomic<uint32_t> consecutive_errors_{0};
    std::atomic<ipb::common::Timestamp> last_error_time_;
    std::atomic<ipb::common::Timestamp> last_successful_operation_;
    
    // Performance tracking
    mutable std::atomic<int64_t> min_operation_time_ns_{INT64_MAX};
    mutable std::atomic<int64_t> max_operation_time_ns_{0};
    mutable std::atomic<int64_t> total_operation_time_ns_{0};
    
    // Internal methods
    ipb::common::Result<> initialize_client();
    void cleanup_client();
    
    ipb::common::Result<> setup_security();
    ipb::common::Result<> setup_realtime_settings();
    
    void polling_loop();
    void subscription_loop();
    void statistics_loop();
    
    ipb::common::Result<ipb::common::DataSet> read_node_ids();
    ipb::common::Result<ipb::common::DataPoint> read_single_node(const NodeId& node_id);
    
    ipb::common::Value convert_ua_variant(const UA_Variant& variant);
    UA_Variant convert_to_ua_variant(const ipb::common::Value& value);
    
    ipb::common::Quality convert_ua_status_code(UA_StatusCode status_code);
    ipb::common::Timestamp convert_ua_datetime(UA_DateTime datetime);
    
    void handle_error(const std::string& error_message, 
                     ipb::common::Result<>::ErrorCode error_code);
    void update_statistics(bool success, std::chrono::nanoseconds duration);
    
    bool should_retry_on_error() const;
    void perform_error_recovery();
    
    // Subscription callbacks
    static void subscription_inactivity_callback(UA_Client* client, 
                                                UA_UInt32 subId, 
                                                void* subContext);
    
    static void delete_subscription_callback(UA_Client* client, 
                                           UA_UInt32 subId, 
                                           void* subContext);
    
    static void data_change_notification_callback(UA_Client* client, 
                                                 UA_UInt32 subId,
                                                 void* subContext, 
                                                 UA_UInt32 monId,
                                                 void* monContext, 
                                                 UA_DataValue* value);
    
    // Batch optimization
    ipb::common::Result<ipb::common::DataSet> read_batch_optimized(const std::vector<NodeId>& node_ids);
};

/**
 * @brief Factory for creating OPC UA adapters
 */
class OPCUAAdapterFactory {
public:
    static std::unique_ptr<OPCUAAdapter> create(const OPCUAAdapterConfig& config);
    static std::unique_ptr<OPCUAAdapter> create_insecure(const std::string& endpoint_url);
    static std::unique_ptr<OPCUAAdapter> create_secure(const std::string& endpoint_url,
                                                      const std::string& username,
                                                      const std::string& password);
    static std::unique_ptr<OPCUAAdapter> create_certificate_based(const std::string& endpoint_url,
                                                                 const std::string& cert_path,
                                                                 const std::string& key_path);
    
    // Preset factories
    static std::unique_ptr<OPCUAAdapter> create_high_performance(const std::string& endpoint_url);
    static std::unique_ptr<OPCUAAdapter> create_low_latency(const std::string& endpoint_url);
    static std::unique_ptr<OPCUAAdapter> create_secure_reliable(const std::string& endpoint_url,
                                                               const std::string& username,
                                                               const std::string& password);
};

} // namespace ipb::adapter::opcua

// Hash specialization for NodeId
namespace std {
    template<>
    struct hash<ipb::adapter::opcua::NodeId> {
        size_t operator()(const ipb::adapter::opcua::NodeId& node_id) const noexcept {
            return node_id.hash();
        }
    };
}

