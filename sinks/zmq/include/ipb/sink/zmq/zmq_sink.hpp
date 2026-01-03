#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

#include <zmq.hpp>

#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/endpoint.hpp"
#include "ipb/common/interfaces.hpp"

namespace ipb::sink::zmq {

/**
 * @brief ZeroMQ socket types
 */
enum class SocketType : uint8_t {
    PUSH = 0,  // Push to pull sockets
    PUB,       // Publish to subscribers
    REQ,       // Request-reply client
    DEALER,    // Asynchronous request-reply
    ROUTER,    // Route messages to dealers
    PAIR,      // Exclusive pair
    STREAM     // TCP stream
};

/**
 * @brief ZeroMQ transport protocols
 */
enum class Transport : uint8_t {
    TCP = 0,  // TCP transport
    IPC,      // Inter-process communication
    INPROC,   // In-process communication
    PGM,      // Pragmatic General Multicast
    EPGM      // Encapsulated PGM
};

/**
 * @brief ZeroMQ security mechanisms
 */
enum class SecurityMechanism : uint8_t {
    NONE = 0,  // No security
    PLAIN,     // Plain text authentication
    CURVE,     // Curve25519 encryption
    GSSAPI     // GSSAPI authentication
};

/**
 * @brief ZeroMQ message serialization format
 */
enum class SerializationFormat : uint8_t { JSON = 0, MSGPACK, PROTOBUF, BINARY, CSV, CUSTOM };

/**
 * @brief ZeroMQ routing strategy for multi-part messages
 */
enum class RoutingStrategy : uint8_t {
    SINGLE_MESSAGE = 0,    // Send as single message
    MULTI_PART_PROTOCOL,   // [protocol_id][address][data]
    MULTI_PART_TIMESTAMP,  // [timestamp][protocol_id][address][data]
    MULTI_PART_CUSTOM      // Custom multi-part format
};

/**
 * @brief ZeroMQ endpoint configuration
 */
struct ZMQEndpoint {
    Transport transport = Transport::TCP;
    std::string address;
    uint16_t port = 0;
    bool bind     = false;  // true = bind, false = connect

    // TCP specific
    bool tcp_keepalive         = true;
    int tcp_keepalive_idle     = 7200;
    int tcp_keepalive_interval = 75;
    int tcp_keepalive_count    = 9;

    // IPC specific
    std::string ipc_path;

    // PGM specific
    std::string pgm_interface;
    int pgm_rate     = 100;    // kbps
    int pgm_recovery = 10000;  // msec

    std::string to_zmq_address() const;
    bool is_valid() const noexcept;
};

/**
 * @brief ZeroMQ sink configuration
 */
class ZMQSinkConfig : public ipb::common::ConfigurationBase {
public:
    // Socket settings
    SocketType socket_type = SocketType::PUSH;
    std::vector<ZMQEndpoint> endpoints;
    int io_threads  = 1;
    int max_sockets = 1024;

    // Connection settings
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds send_timeout{1000};
    std::chrono::milliseconds recv_timeout{1000};
    int linger_time = 1000;  // milliseconds
    bool immediate  = false;

    // Buffer settings
    int send_hwm         = 1000;  // High water mark for outbound messages
    int recv_hwm         = 1000;  // High water mark for inbound messages
    int send_buffer_size = 0;     // 0 = use OS default
    int recv_buffer_size = 0;     // 0 = use OS default

    // Security settings
    SecurityMechanism security_mechanism = SecurityMechanism::NONE;
    std::string plain_username;
    std::string plain_password;
    std::string curve_server_key;
    std::string curve_public_key;
    std::string curve_secret_key;
    std::string gssapi_principal;
    std::string gssapi_service_principal;

    // Message settings
    SerializationFormat serialization_format = SerializationFormat::JSON;
    RoutingStrategy routing_strategy         = RoutingStrategy::SINGLE_MESSAGE;
    bool enable_compression                  = false;
    std::string compression_algorithm        = "zlib";  // zlib, lz4, zstd
    int compression_level                    = 6;

    // Performance settings
    uint32_t max_batch_size = 1000;
    std::chrono::milliseconds flush_interval{10};
    bool enable_async_send       = true;
    uint32_t worker_thread_count = 1;
    uint32_t queue_size          = 10000;
    bool enable_zero_copy        = true;

    // Real-time settings
    bool enable_realtime_priority = false;
    int realtime_priority         = 50;
    int cpu_affinity              = -1;

    // Load balancing (for multiple endpoints)
    bool enable_load_balancing        = true;
    std::string load_balance_strategy = "round_robin";  // round_robin, random, hash

    // Error handling
    bool enable_error_recovery      = true;
    uint32_t max_consecutive_errors = 100;
    std::chrono::milliseconds error_backoff_time{100};
    bool enable_automatic_reconnection = true;
    std::chrono::milliseconds reconnection_interval{1000};

    // Monitoring
    bool enable_statistics = true;
    std::chrono::milliseconds statistics_interval{1000};
    bool enable_zmq_monitoring = false;

    // ConfigurationBase interface
    ipb::common::Result<> validate() const override;
    std::string to_string() const override;
    ipb::common::Result<> from_string(std::string_view config) override;
    std::unique_ptr<ipb::common::ConfigurationBase> clone() const override;

    // Preset configurations
    static ZMQSinkConfig create_high_throughput();
    static ZMQSinkConfig create_low_latency();
    static ZMQSinkConfig create_reliable();
    static ZMQSinkConfig create_secure();
};

/**
 * @brief High-performance ZeroMQ data sink
 *
 * Features:
 * - Ultra-low latency messaging (sub-millisecond)
 * - Zero-copy operations where possible
 * - Multiple socket patterns (PUSH/PULL, PUB/SUB, REQ/REP)
 * - Comprehensive security (CURVE, PLAIN, GSSAPI)
 * - Load balancing across multiple endpoints
 * - Automatic reconnection and error recovery
 * - Real-time performance monitoring
 * - Compression support for bandwidth optimization
 * - Multi-part message routing
 */
class ZMQSink : public ipb::common::IIPBSinkBase {
public:
    static constexpr std::string_view SINK_TYPE         = "ZeroMQ";
    static constexpr std::string_view COMPONENT_NAME    = "ZMQSink";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";

    ZMQSink();
    ~ZMQSink() override;

    // Disable copy/move for thread safety
    ZMQSink(const ZMQSink&)            = delete;
    ZMQSink& operator=(const ZMQSink&) = delete;
    ZMQSink(ZMQSink&&)                 = delete;
    ZMQSink& operator=(ZMQSink&&)      = delete;

    // IIPBSinkBase interface
    ipb::common::Result<> write(const ipb::common::DataPoint& data_point) override;
    ipb::common::Result<> write_batch(std::span<const ipb::common::DataPoint> data_points) override;
    ipb::common::Result<> write_dataset(const ipb::common::DataSet& dataset) override;

    std::future<ipb::common::Result<>> write_async(
        const ipb::common::DataPoint& data_point) override;
    std::future<ipb::common::Result<>> write_batch_async(
        std::span<const ipb::common::DataPoint> data_points) override;

    ipb::common::Result<> flush() override;
    size_t pending_count() const noexcept override;
    bool can_accept_data() const noexcept override;

    std::string_view sink_type() const noexcept override { return SINK_TYPE; }
    size_t max_batch_size() const noexcept override;

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

    // ZeroMQ-specific methods
    ipb::common::Result<> send_raw_message(const std::string& message);
    ipb::common::Result<> send_multipart_message(const std::vector<std::string>& parts);
    ipb::common::Result<> send_binary_message(std::span<const uint8_t> data);

    // Custom serialization
    using CustomSerializer = std::function<std::string(const ipb::common::DataPoint&)>;
    void set_custom_serializer(CustomSerializer serializer);

    // Custom routing for multi-part messages
    using CustomRouter = std::function<std::vector<std::string>(const ipb::common::DataPoint&)>;
    void set_custom_router(CustomRouter router);

    // Socket monitoring
    ipb::common::Result<> enable_monitoring(const std::string& monitor_endpoint);
    ipb::common::Result<> disable_monitoring();

    // Connection management
    ipb::common::Result<> add_endpoint(const ZMQEndpoint& endpoint);
    ipb::common::Result<> remove_endpoint(const std::string& address);
    std::vector<ZMQEndpoint> get_endpoints() const;

    // Security key management
    ipb::common::Result<> generate_curve_keypair();
    ipb::common::Result<> set_curve_keys(const std::string& public_key,
                                         const std::string& secret_key);
    std::pair<std::string, std::string> get_curve_keys() const;

private:
    // Configuration
    std::unique_ptr<ZMQSinkConfig> config_;

    // ZeroMQ context and socket
    std::unique_ptr<zmq::context_t> zmq_context_;
    std::unique_ptr<zmq::socket_t> zmq_socket_;
    std::unique_ptr<zmq::socket_t> monitor_socket_;

    // State management
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> monitoring_enabled_{false};

    // Threading
    std::vector<std::unique_ptr<std::thread>> worker_threads_;
    std::unique_ptr<std::thread> flush_thread_;
    std::unique_ptr<std::thread> monitor_thread_;
    std::unique_ptr<std::thread> statistics_thread_;
    mutable std::mutex state_mutex_;
    std::condition_variable stop_condition_;

    // Message queue
    struct QueuedMessage {
        ipb::common::DataPoint data_point;
        std::string serialized_data;
        std::vector<std::string> multipart_data;
        std::promise<ipb::common::Result<>> promise;
        ipb::common::Timestamp enqueue_time;
        bool is_multipart = false;
    };

    std::queue<QueuedMessage> message_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::atomic<size_t> queue_size_{0};

    // Endpoint management
    std::vector<ZMQEndpoint> endpoints_;
    std::atomic<size_t> current_endpoint_index_{0};
    mutable std::shared_mutex endpoints_mutex_;

    // Custom functions
    CustomSerializer custom_serializer_;
    CustomRouter custom_router_;

    // Statistics (lock-free)
    mutable std::atomic<uint64_t> total_messages_{0};
    mutable std::atomic<uint64_t> successful_messages_{0};
    mutable std::atomic<uint64_t> failed_messages_{0};
    mutable std::atomic<uint64_t> total_bytes_{0};
    mutable std::atomic<uint64_t> total_batches_{0};
    mutable std::atomic<uint64_t> queue_overflows_{0};
    mutable std::atomic<uint64_t> reconnections_{0};

    // Error tracking
    std::atomic<uint32_t> consecutive_errors_{0};
    std::atomic<ipb::common::Timestamp> last_error_time_;
    std::atomic<ipb::common::Timestamp> last_successful_send_;

    // Performance tracking
    mutable std::atomic<int64_t> min_send_time_ns_{INT64_MAX};
    mutable std::atomic<int64_t> max_send_time_ns_{0};
    mutable std::atomic<int64_t> total_send_time_ns_{0};

    // Load balancing
    size_t get_next_endpoint_index();
    size_t get_hash_based_endpoint_index(const ipb::common::DataPoint& data_point);

    // Internal methods
    ipb::common::Result<> initialize_zmq();
    void cleanup_zmq();

    ipb::common::Result<> setup_socket();
    ipb::common::Result<> setup_security();
    ipb::common::Result<> setup_endpoints();
    ipb::common::Result<> setup_realtime_settings();

    void worker_loop(int worker_id);
    void flush_loop();
    void monitor_loop();
    void statistics_loop();

    ipb::common::Result<> send_message_internal(QueuedMessage&& message);
    ipb::common::Result<> enqueue_message(QueuedMessage&& message);

    std::string serialize_data_point(const ipb::common::DataPoint& data_point);
    std::string serialize_json(const ipb::common::DataPoint& data_point);
    std::string serialize_msgpack(const ipb::common::DataPoint& data_point);
    std::string serialize_protobuf(const ipb::common::DataPoint& data_point);
    std::string serialize_binary(const ipb::common::DataPoint& data_point);
    std::string serialize_csv(const ipb::common::DataPoint& data_point);

    std::vector<std::string> create_multipart_message(const ipb::common::DataPoint& data_point);

    std::string compress_data(const std::string& data);
    std::string decompress_data(const std::string& compressed_data);

    void handle_error(const std::string& error_message,
                      ipb::common::Result<>::ErrorCode error_code);
    void update_statistics(bool success, std::chrono::nanoseconds duration, size_t bytes = 0);

    bool should_retry_on_error() const;
    void perform_error_recovery();
    void perform_reconnection();

    // ZeroMQ socket options
    void configure_socket_options();
    void configure_security_options();
    void configure_performance_options();

    // Monitoring event handling
    void handle_monitor_event(const zmq::message_t& event_msg);
    std::string monitor_event_to_string(int event);
};

/**
 * @brief Factory for creating ZeroMQ sinks
 */
class ZMQSinkFactory {
public:
    static std::unique_ptr<ZMQSink> create(const ZMQSinkConfig& config);
    static std::unique_ptr<ZMQSink> create_push(const std::string& address, uint16_t port);
    static std::unique_ptr<ZMQSink> create_pub(const std::string& address, uint16_t port);
    static std::unique_ptr<ZMQSink> create_req(const std::string& address, uint16_t port);

    // IPC variants
    static std::unique_ptr<ZMQSink> create_push_ipc(const std::string& ipc_path);
    static std::unique_ptr<ZMQSink> create_pub_ipc(const std::string& ipc_path);

    // Secure variants
    static std::unique_ptr<ZMQSink> create_secure_push(const std::string& address, uint16_t port,
                                                       const std::string& server_key);
    static std::unique_ptr<ZMQSink> create_secure_pub(const std::string& address, uint16_t port,
                                                      const std::string& server_key);

    // Preset factories
    static std::unique_ptr<ZMQSink> create_high_throughput(const std::string& address,
                                                           uint16_t port);
    static std::unique_ptr<ZMQSink> create_low_latency(const std::string& address, uint16_t port);
    static std::unique_ptr<ZMQSink> create_reliable(const std::string& address, uint16_t port);
};

}  // namespace ipb::sink::zmq
