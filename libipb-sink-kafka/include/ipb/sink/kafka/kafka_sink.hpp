#pragma once

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/endpoint.hpp"
#include <librdkafka/rdkafka.h>
#include <librdkafka/rdkafkacpp.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <future>

namespace ipb::sink::kafka {

/**
 * @brief Kafka message serialization format
 */
enum class SerializationFormat : uint8_t {
    JSON = 0,
    AVRO,
    PROTOBUF,
    BINARY,
    CSV,
    CUSTOM
};

/**
 * @brief Kafka partitioning strategy
 */
enum class PartitioningStrategy : uint8_t {
    ROUND_ROBIN = 0,
    HASH_BY_ADDRESS,
    HASH_BY_PROTOCOL,
    HASH_BY_TIMESTAMP,
    CUSTOM,
    MANUAL
};

/**
 * @brief Kafka compression type
 */
enum class CompressionType : uint8_t {
    NONE = 0,
    GZIP,
    SNAPPY,
    LZ4,
    ZSTD
};

/**
 * @brief Kafka delivery guarantee
 */
enum class DeliveryGuarantee : uint8_t {
    AT_MOST_ONCE = 0,   // acks=0
    AT_LEAST_ONCE = 1,  // acks=1
    EXACTLY_ONCE = 2    // acks=all + idempotent
};

/**
 * @brief Kafka topic configuration
 */
struct TopicConfig {
    std::string topic_name;
    int32_t partition = -1; // -1 for automatic partitioning
    PartitioningStrategy partitioning_strategy = PartitioningStrategy::HASH_BY_ADDRESS;
    std::string key_template = "{protocol_id}:{address}";
    
    // Topic creation settings (if auto-create is enabled)
    int32_t num_partitions = 3;
    int16_t replication_factor = 1;
    std::unordered_map<std::string, std::string> topic_config_overrides;
    
    bool is_valid() const noexcept;
};

/**
 * @brief Kafka sink configuration
 */
class KafkaSinkConfig : public ipb::common::ConfigurationBase {
public:
    // Broker settings
    std::vector<std::string> bootstrap_servers;
    std::string client_id = "ipb-kafka-sink";
    std::chrono::milliseconds metadata_timeout{30000};
    std::chrono::milliseconds request_timeout{30000};
    
    // Security settings
    std::string security_protocol = "PLAINTEXT"; // PLAINTEXT, SSL, SASL_PLAINTEXT, SASL_SSL
    std::string sasl_mechanism = "PLAIN";        // PLAIN, SCRAM-SHA-256, SCRAM-SHA-512, GSSAPI
    std::string sasl_username;
    std::string sasl_password;
    std::string ssl_ca_location;
    std::string ssl_certificate_location;
    std::string ssl_key_location;
    std::string ssl_key_password;
    bool ssl_verify_hostname = true;
    
    // Producer settings
    DeliveryGuarantee delivery_guarantee = DeliveryGuarantee::AT_LEAST_ONCE;
    CompressionType compression = CompressionType::SNAPPY;
    int32_t batch_size = 16384;
    std::chrono::milliseconds linger_ms{5};
    int32_t buffer_memory = 33554432; // 32MB
    int32_t max_in_flight_requests = 5;
    bool enable_idempotence = true;
    int32_t retries = INT32_MAX;
    std::chrono::milliseconds retry_backoff_ms{100};
    
    // Topic settings
    std::vector<TopicConfig> topics;
    TopicConfig default_topic;
    bool enable_topic_auto_creation = false;
    
    // Serialization settings
    SerializationFormat serialization_format = SerializationFormat::JSON;
    bool include_metadata = true;
    bool include_timestamp = true;
    bool include_quality = true;
    bool include_protocol_info = true;
    std::string custom_schema_registry_url;
    
    // Performance settings
    uint32_t max_batch_size = 1000;
    std::chrono::milliseconds flush_interval{100};
    bool enable_async_send = true;
    uint32_t worker_thread_count = 2;
    uint32_t queue_size = 10000;
    
    // Real-time settings
    bool enable_realtime_priority = false;
    int realtime_priority = 50;
    int cpu_affinity = -1;
    
    // Error handling
    bool enable_error_recovery = true;
    uint32_t max_consecutive_errors = 100;
    std::chrono::milliseconds error_backoff_time{1000};
    bool enable_dead_letter_queue = false;
    std::string dead_letter_topic = "ipb-dlq";
    
    // Monitoring
    bool enable_statistics = true;
    std::chrono::milliseconds statistics_interval{1000};
    bool enable_kafka_statistics = false;
    std::chrono::milliseconds kafka_statistics_interval{5000};
    
    // ConfigurationBase interface
    ipb::common::Result<> validate() const override;
    std::string to_string() const override;
    ipb::common::Result<> from_string(std::string_view config) override;
    std::unique_ptr<ipb::common::ConfigurationBase> clone() const override;
    
    // Preset configurations
    static KafkaSinkConfig create_high_throughput();
    static KafkaSinkConfig create_low_latency();
    static KafkaSinkConfig create_reliable();
    static KafkaSinkConfig create_exactly_once();
};

/**
 * @brief High-performance Kafka data sink
 * 
 * Features:
 * - High-throughput batch processing
 * - Multiple serialization formats (JSON, Avro, Protobuf)
 * - Flexible partitioning strategies
 * - Comprehensive security support
 * - Exactly-once delivery semantics
 * - Real-time performance monitoring
 * - Automatic error recovery
 * - Dead letter queue support
 * - Schema registry integration
 */
class KafkaSink : public ipb::common::IIPBSinkBase {
public:
    static constexpr std::string_view SINK_TYPE = "Kafka";
    static constexpr std::string_view COMPONENT_NAME = "KafkaSink";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";
    
    KafkaSink();
    ~KafkaSink() override;
    
    // Disable copy/move for thread safety
    KafkaSink(const KafkaSink&) = delete;
    KafkaSink& operator=(const KafkaSink&) = delete;
    KafkaSink(KafkaSink&&) = delete;
    KafkaSink& operator=(KafkaSink&&) = delete;
    
    // IIPBSinkBase interface
    ipb::common::Result<> write(const ipb::common::DataPoint& data_point) override;
    ipb::common::Result<> write_batch(std::span<const ipb::common::DataPoint> data_points) override;
    ipb::common::Result<> write_dataset(const ipb::common::DataSet& dataset) override;
    
    std::future<ipb::common::Result<>> write_async(const ipb::common::DataPoint& data_point) override;
    std::future<ipb::common::Result<>> write_batch_async(std::span<const ipb::common::DataPoint> data_points) override;
    
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
    
    // Kafka-specific methods
    ipb::common::Result<> create_topic(const TopicConfig& topic_config);
    ipb::common::Result<> delete_topic(const std::string& topic_name);
    ipb::common::Result<std::vector<std::string>> list_topics();
    ipb::common::Result<std::unordered_map<std::string, std::string>> get_topic_metadata(const std::string& topic_name);
    
    // Custom serialization
    using CustomSerializer = std::function<std::string(const ipb::common::DataPoint&)>;
    void set_custom_serializer(CustomSerializer serializer);
    
    // Custom partitioning
    using CustomPartitioner = std::function<int32_t(const ipb::common::DataPoint&, int32_t num_partitions)>;
    void set_custom_partitioner(CustomPartitioner partitioner);
    
    // Transaction support (for exactly-once semantics)
    ipb::common::Result<> begin_transaction();
    ipb::common::Result<> commit_transaction();
    ipb::common::Result<> abort_transaction();
    
    // Schema registry integration
    ipb::common::Result<> register_schema(const std::string& subject, const std::string& schema);
    ipb::common::Result<std::string> get_schema(const std::string& subject, int version = -1);

private:
    // Configuration
    std::unique_ptr<KafkaSinkConfig> config_;
    
    // Kafka producer
    std::unique_ptr<RdKafka::Producer> producer_;
    std::unique_ptr<RdKafka::Conf> kafka_conf_;
    std::unique_ptr<RdKafka::Conf> topic_conf_;
    
    // State management
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> in_transaction_{false};
    
    // Threading
    std::vector<std::unique_ptr<std::thread>> worker_threads_;
    std::unique_ptr<std::thread> flush_thread_;
    std::unique_ptr<std::thread> statistics_thread_;
    mutable std::mutex state_mutex_;
    std::condition_variable stop_condition_;
    
    // Message queue
    struct QueuedMessage {
        ipb::common::DataPoint data_point;
        std::string topic;
        int32_t partition;
        std::string key;
        std::string payload;
        std::promise<ipb::common::Result<>> promise;
        ipb::common::Timestamp enqueue_time;
    };
    
    std::queue<QueuedMessage> message_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::atomic<size_t> queue_size_{0};
    
    // Topic management
    std::unordered_map<std::string, TopicConfig> topic_configs_;
    mutable std::shared_mutex topics_mutex_;
    
    // Custom functions
    CustomSerializer custom_serializer_;
    CustomPartitioner custom_partitioner_;
    
    // Statistics (lock-free)
    mutable std::atomic<uint64_t> total_messages_{0};
    mutable std::atomic<uint64_t> successful_messages_{0};
    mutable std::atomic<uint64_t> failed_messages_{0};
    mutable std::atomic<uint64_t> total_bytes_{0};
    mutable std::atomic<uint64_t> total_batches_{0};
    mutable std::atomic<uint64_t> queue_overflows_{0};
    
    // Error tracking
    std::atomic<uint32_t> consecutive_errors_{0};
    std::atomic<ipb::common::Timestamp> last_error_time_;
    std::atomic<ipb::common::Timestamp> last_successful_send_;
    
    // Performance tracking
    mutable std::atomic<int64_t> min_send_time_ns_{INT64_MAX};
    mutable std::atomic<int64_t> max_send_time_ns_{0};
    mutable std::atomic<int64_t> total_send_time_ns_{0};
    
    // Delivery tracking
    struct DeliveryContext {
        std::promise<ipb::common::Result<>> promise;
        ipb::common::Timestamp send_time;
        size_t message_size;
    };
    std::unordered_map<void*, std::unique_ptr<DeliveryContext>> delivery_contexts_;
    mutable std::mutex delivery_mutex_;
    
    // Internal methods
    ipb::common::Result<> initialize_kafka();
    void cleanup_kafka();
    
    ipb::common::Result<> setup_security();
    ipb::common::Result<> setup_realtime_settings();
    
    void worker_loop(int worker_id);
    void flush_loop();
    void statistics_loop();
    
    ipb::common::Result<> send_message_internal(QueuedMessage&& message);
    ipb::common::Result<> enqueue_message(QueuedMessage&& message);
    
    std::string serialize_data_point(const ipb::common::DataPoint& data_point);
    std::string serialize_json(const ipb::common::DataPoint& data_point);
    std::string serialize_avro(const ipb::common::DataPoint& data_point);
    std::string serialize_protobuf(const ipb::common::DataPoint& data_point);
    std::string serialize_binary(const ipb::common::DataPoint& data_point);
    std::string serialize_csv(const ipb::common::DataPoint& data_point);
    
    std::string generate_message_key(const ipb::common::DataPoint& data_point, const TopicConfig& topic_config);
    int32_t determine_partition(const ipb::common::DataPoint& data_point, const TopicConfig& topic_config);
    
    TopicConfig get_topic_config_for_data_point(const ipb::common::DataPoint& data_point);
    
    void handle_error(const std::string& error_message, 
                     ipb::common::Result<>::ErrorCode error_code);
    void update_statistics(bool success, std::chrono::nanoseconds duration, size_t bytes = 0);
    
    bool should_retry_on_error() const;
    void perform_error_recovery();
    
    // Kafka callbacks
    class DeliveryReportCallback : public RdKafka::DeliveryReportCb {
    public:
        explicit DeliveryReportCallback(KafkaSink* sink) : sink_(sink) {}
        void dr_cb(RdKafka::Message& message) override;
    private:
        KafkaSink* sink_;
    };
    
    class EventCallback : public RdKafka::EventCb {
    public:
        explicit EventCallback(KafkaSink* sink) : sink_(sink) {}
        void event_cb(RdKafka::Event& event) override;
    private:
        KafkaSink* sink_;
    };
    
    std::unique_ptr<DeliveryReportCallback> delivery_callback_;
    std::unique_ptr<EventCallback> event_callback_;
    
    // Dead letter queue
    ipb::common::Result<> send_to_dead_letter_queue(const ipb::common::DataPoint& data_point, 
                                                   const std::string& error_reason);
};

/**
 * @brief Factory for creating Kafka sinks
 */
class KafkaSinkFactory {
public:
    static std::unique_ptr<KafkaSink> create(const KafkaSinkConfig& config);
    static std::unique_ptr<KafkaSink> create_simple(const std::vector<std::string>& bootstrap_servers,
                                                   const std::string& topic_name);
    static std::unique_ptr<KafkaSink> create_secure(const std::vector<std::string>& bootstrap_servers,
                                                   const std::string& topic_name,
                                                   const std::string& username,
                                                   const std::string& password);
    
    // Preset factories
    static std::unique_ptr<KafkaSink> create_high_throughput(const std::vector<std::string>& bootstrap_servers,
                                                            const std::string& topic_name);
    static std::unique_ptr<KafkaSink> create_low_latency(const std::vector<std::string>& bootstrap_servers,
                                                        const std::string& topic_name);
    static std::unique_ptr<KafkaSink> create_exactly_once(const std::vector<std::string>& bootstrap_servers,
                                                         const std::string& topic_name);
};

} // namespace ipb::sink::kafka

