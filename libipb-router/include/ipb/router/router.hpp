#pragma once

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/endpoint.hpp"
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
#include <functional>
#include <regex>
#include <future>

namespace ipb::router {

/**
 * @brief Routing rule types
 */
enum class RuleType : uint8_t {
    STATIC = 0,         // Static address-based routing
    PROTOCOL_BASED,     // Route based on protocol ID
    REGEX_PATTERN,      // Route based on regex pattern matching
    QUALITY_BASED,      // Route based on data quality
    TIMESTAMP_BASED,    // Route based on timestamp ranges
    VALUE_BASED,        // Route based on data value conditions
    CUSTOM_LOGIC,       // Custom logic function
    LOAD_BALANCING,     // Load balancing across multiple sinks
    FAILOVER,           // Failover to backup sinks
    BROADCAST           // Broadcast to all matching sinks
};

/**
 * @brief Routing priority levels
 */
enum class RoutingPriority : uint8_t {
    LOWEST = 0,
    LOW = 64,
    NORMAL = 128,
    HIGH = 192,
    HIGHEST = 255,
    REALTIME = 254     // Special priority for real-time data
};

/**
 * @brief Load balancing strategies
 */
enum class LoadBalanceStrategy : uint8_t {
    ROUND_ROBIN = 0,
    WEIGHTED_ROUND_ROBIN,
    LEAST_CONNECTIONS,
    LEAST_LATENCY,
    HASH_BASED,
    RANDOM,
    CUSTOM
};

/**
 * @brief Routing condition for value-based routing
 */
struct ValueCondition {
    enum class Operator : uint8_t {
        EQUAL, NOT_EQUAL, LESS_THAN, LESS_EQUAL, 
        GREATER_THAN, GREATER_EQUAL, CONTAINS, REGEX_MATCH
    };
    
    Operator op = Operator::EQUAL;
    ipb::common::Value reference_value;
    std::string regex_pattern;
    
    bool evaluate(const ipb::common::Value& value) const;
};

/**
 * @brief Routing rule definition
 */
struct RoutingRule {
    uint32_t rule_id = 0;
    std::string name;
    RuleType type = RuleType::STATIC;
    RoutingPriority priority = RoutingPriority::NORMAL;
    bool enabled = true;
    
    // Rule conditions
    std::vector<std::string> source_addresses;      // Static addresses
    std::vector<uint16_t> protocol_ids;             // Protocol IDs
    std::string address_pattern;                    // Regex pattern for addresses
    std::vector<ipb::common::Quality> quality_levels; // Quality conditions
    ipb::common::Timestamp start_time;              // Timestamp range start
    ipb::common::Timestamp end_time;                // Timestamp range end
    std::vector<ValueCondition> value_conditions;   // Value-based conditions
    
    // Target sinks
    std::vector<std::string> target_sink_ids;
    LoadBalanceStrategy load_balance_strategy = LoadBalanceStrategy::ROUND_ROBIN;
    std::vector<uint32_t> sink_weights;             // For weighted load balancing
    
    // Failover settings
    bool enable_failover = false;
    std::vector<std::string> backup_sink_ids;
    std::chrono::milliseconds failover_timeout{5000};
    
    // Custom logic function
    std::function<bool(const ipb::common::DataPoint&)> custom_condition;
    std::function<std::vector<std::string>(const ipb::common::DataPoint&)> custom_target_selector;
    
    // Performance settings
    bool enable_batching = false;
    uint32_t batch_size = 100;
    std::chrono::milliseconds batch_timeout{10};
    
    // Statistics
    mutable std::atomic<uint64_t> match_count{0};
    mutable std::atomic<uint64_t> success_count{0};
    mutable std::atomic<uint64_t> failure_count{0};
    mutable std::atomic<int64_t> total_processing_time_ns{0};
    
    // Validation
    bool is_valid() const noexcept;
    
    // Evaluation
    bool matches(const ipb::common::DataPoint& data_point) const;
    std::vector<std::string> get_target_sinks(const ipb::common::DataPoint& data_point) const;
};

/**
 * @brief EDF (Earliest Deadline First) task for real-time scheduling
 */
struct EDFTask {
    uint64_t task_id;
    ipb::common::Timestamp deadline;
    ipb::common::Timestamp arrival_time;
    RoutingPriority priority;
    std::function<void()> task_function;
    
    // For priority queue (min-heap based on deadline)
    bool operator>(const EDFTask& other) const {
        return deadline > other.deadline;
    }
};

/**
 * @brief Router configuration
 */
class RouterConfig : public ipb::common::ConfigurationBase {
public:
    // Threading settings
    uint32_t worker_thread_count = std::thread::hardware_concurrency();
    uint32_t edf_scheduler_thread_count = 1;
    bool enable_thread_affinity = false;
    std::vector<int> thread_cpu_affinity;
    
    // Real-time settings
    bool enable_realtime_scheduling = false;
    int realtime_priority = 50;
    bool enable_edf_scheduling = true;
    std::chrono::nanoseconds default_deadline_offset{1000000}; // 1ms
    
    // Performance settings
    uint32_t input_queue_size = 100000;
    uint32_t output_queue_size = 10000;
    bool enable_zero_copy = true;
    bool enable_lock_free_queues = true;
    uint32_t batch_processing_size = 1000;
    
    // Memory management
    bool enable_memory_pool = true;
    uint32_t memory_pool_size = 10000;
    bool enable_numa_awareness = false;
    
    // Error handling
    bool enable_error_recovery = true;
    uint32_t max_consecutive_errors = 1000;
    std::chrono::milliseconds error_backoff_time{100};
    bool enable_dead_letter_queue = true;
    std::string dead_letter_sink_id = "dead_letter_sink";
    
    // Monitoring
    bool enable_statistics = true;
    std::chrono::milliseconds statistics_interval{1000};
    bool enable_performance_monitoring = true;
    bool enable_deadline_monitoring = true;
    
    // Hot reload
    bool enable_hot_reload = true;
    std::chrono::milliseconds rule_reload_check_interval{5000};
    
    // ConfigurationBase interface
    ipb::common::Result<> validate() const override;
    std::string to_string() const override;
    ipb::common::Result<> from_string(std::string_view config) override;
    std::unique_ptr<ipb::common::ConfigurationBase> clone() const override;
    
    // Preset configurations
    static RouterConfig create_high_throughput();
    static RouterConfig create_low_latency();
    static RouterConfig create_realtime();
    static RouterConfig create_reliable();
};

/**
 * @brief High-performance message router with EDF scheduling
 * 
 * Features:
 * - EDF (Earliest Deadline First) real-time scheduling
 * - Lock-free message queues for ultra-low latency
 * - Dynamic routing rules with hot reload
 * - Multiple routing strategies (static, regex, custom logic)
 * - Load balancing and failover support
 * - Batch processing optimization
 * - NUMA-aware memory allocation
 * - Comprehensive performance monitoring
 * - Zero-copy message handling
 * - Thread pool with CPU affinity
 */
class Router : public ipb::common::IIPBComponent {
public:
    static constexpr std::string_view COMPONENT_NAME = "IPBRouter";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";
    
    Router();
    ~Router() override;
    
    // Disable copy/move for thread safety
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&) = delete;
    Router& operator=(Router&&) = delete;
    
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
    
    // Sink management
    ipb::common::Result<> register_sink(const std::string& sink_id, 
                                       std::shared_ptr<ipb::common::IIPBSink> sink);
    ipb::common::Result<> unregister_sink(const std::string& sink_id);
    std::vector<std::string> get_registered_sinks() const;
    
    // Routing rule management
    ipb::common::Result<uint32_t> add_routing_rule(const RoutingRule& rule);
    ipb::common::Result<> update_routing_rule(uint32_t rule_id, const RoutingRule& rule);
    ipb::common::Result<> remove_routing_rule(uint32_t rule_id);
    ipb::common::Result<> enable_routing_rule(uint32_t rule_id, bool enabled = true);
    std::vector<RoutingRule> get_routing_rules() const;
    ipb::common::Result<RoutingRule> get_routing_rule(uint32_t rule_id) const;
    
    // Message routing
    ipb::common::Result<> route_message(const ipb::common::DataPoint& data_point);
    ipb::common::Result<> route_message_with_deadline(const ipb::common::DataPoint& data_point,
                                                     ipb::common::Timestamp deadline);
    ipb::common::Result<> route_batch(std::span<const ipb::common::DataPoint> data_points);
    ipb::common::Result<> route_dataset(const ipb::common::DataSet& dataset);
    
    // Asynchronous routing
    std::future<ipb::common::Result<>> route_message_async(const ipb::common::DataPoint& data_point);
    std::future<ipb::common::Result<>> route_batch_async(std::span<const ipb::common::DataPoint> data_points);
    
    // Load balancing management
    ipb::common::Result<> set_sink_weight(const std::string& sink_id, uint32_t weight);
    ipb::common::Result<uint32_t> get_sink_weight(const std::string& sink_id) const;
    ipb::common::Result<> enable_sink(const std::string& sink_id, bool enabled = true);
    
    // Performance monitoring
    struct PerformanceMetrics {
        uint64_t total_messages_routed = 0;
        uint64_t successful_routes = 0;
        uint64_t failed_routes = 0;
        uint64_t deadlines_missed = 0;
        uint64_t deadlines_met = 0;
        
        std::chrono::nanoseconds avg_routing_time{0};
        std::chrono::nanoseconds min_routing_time{std::chrono::nanoseconds::max()};
        std::chrono::nanoseconds max_routing_time{0};
        
        uint64_t queue_overflows = 0;
        uint64_t memory_pool_exhaustions = 0;
        
        std::unordered_map<std::string, uint64_t> sink_message_counts;
        std::unordered_map<uint32_t, uint64_t> rule_match_counts;
    };
    
    PerformanceMetrics get_performance_metrics() const;
    void reset_performance_metrics();
    
    // EDF scheduler control
    ipb::common::Result<> set_default_deadline_offset(std::chrono::nanoseconds offset);
    std::chrono::nanoseconds get_default_deadline_offset() const;
    uint64_t get_pending_task_count() const;
    uint64_t get_missed_deadline_count() const;
    
    // Hot reload
    ipb::common::Result<> reload_routing_rules();
    ipb::common::Result<> load_routing_rules_from_file(const std::string& file_path);
    ipb::common::Result<> save_routing_rules_to_file(const std::string& file_path) const;

private:
    // Configuration
    std::unique_ptr<RouterConfig> config_;
    
    // State management
    std::atomic<bool> is_running_{false};
    std::atomic<bool> shutdown_requested_{false};
    
    // Sink registry
    std::unordered_map<std::string, std::shared_ptr<ipb::common::IIPBSink>> sinks_;
    std::unordered_map<std::string, uint32_t> sink_weights_;
    std::unordered_map<std::string, bool> sink_enabled_;
    std::unordered_map<std::string, std::atomic<uint64_t>> sink_round_robin_counters_;
    mutable std::shared_mutex sinks_mutex_;
    
    // Routing rules
    std::unordered_map<uint32_t, RoutingRule> routing_rules_;
    std::atomic<uint32_t> next_rule_id_{1};
    mutable std::shared_mutex rules_mutex_;
    
    // Threading
    std::vector<std::unique_ptr<std::thread>> worker_threads_;
    std::vector<std::unique_ptr<std::thread>> edf_scheduler_threads_;
    std::unique_ptr<std::thread> statistics_thread_;
    std::unique_ptr<std::thread> hot_reload_thread_;
    
    // Message queues (lock-free)
    using MessageQueue = ipb::common::rt::SPSCRingBuffer<ipb::common::DataPoint, 65536>;
    std::vector<std::unique_ptr<MessageQueue>> input_queues_;
    std::atomic<size_t> next_queue_index_{0};
    
    // EDF scheduler
    std::priority_queue<EDFTask, std::vector<EDFTask>, std::greater<EDFTask>> edf_task_queue_;
    mutable std::mutex edf_mutex_;
    std::condition_variable edf_condition_;
    std::atomic<uint64_t> next_task_id_{1};
    std::atomic<std::chrono::nanoseconds> default_deadline_offset_{1000000}; // 1ms
    
    // Memory pool
    using DataPointPool = ipb::common::rt::MemoryPool<ipb::common::DataPoint, 10000>;
    std::unique_ptr<DataPointPool> memory_pool_;
    
    // Statistics (lock-free)
    mutable std::atomic<uint64_t> total_messages_routed_{0};
    mutable std::atomic<uint64_t> successful_routes_{0};
    mutable std::atomic<uint64_t> failed_routes_{0};
    mutable std::atomic<uint64_t> deadlines_missed_{0};
    mutable std::atomic<uint64_t> deadlines_met_{0};
    mutable std::atomic<uint64_t> queue_overflows_{0};
    mutable std::atomic<uint64_t> memory_pool_exhaustions_{0};
    
    // Performance tracking
    mutable std::atomic<int64_t> total_routing_time_ns_{0};
    mutable std::atomic<int64_t> min_routing_time_ns_{INT64_MAX};
    mutable std::atomic<int64_t> max_routing_time_ns_{0};
    
    // Error tracking
    std::atomic<uint32_t> consecutive_errors_{0};
    std::atomic<ipb::common::Timestamp> last_error_time_;
    
    // Internal methods
    ipb::common::Result<> initialize_threads();
    ipb::common::Result<> setup_realtime_settings();
    ipb::common::Result<> setup_memory_pools();
    
    void worker_loop(int worker_id);
    void edf_scheduler_loop(int scheduler_id);
    void statistics_loop();
    void hot_reload_loop();
    
    ipb::common::Result<> process_message(const ipb::common::DataPoint& data_point);
    ipb::common::Result<> process_message_with_deadline(const ipb::common::DataPoint& data_point,
                                                       ipb::common::Timestamp deadline);
    
    std::vector<std::string> find_matching_sinks(const ipb::common::DataPoint& data_point);
    std::vector<RoutingRule*> find_matching_rules(const ipb::common::DataPoint& data_point);
    
    std::string select_sink_with_load_balancing(const std::vector<std::string>& sink_ids,
                                               LoadBalanceStrategy strategy,
                                               const ipb::common::DataPoint& data_point);
    
    ipb::common::Result<> send_to_sink(const std::string& sink_id, 
                                      const ipb::common::DataPoint& data_point);
    ipb::common::Result<> send_to_dead_letter_queue(const ipb::common::DataPoint& data_point,
                                                   const std::string& error_reason);
    
    void schedule_edf_task(std::function<void()> task_function, 
                          ipb::common::Timestamp deadline,
                          RoutingPriority priority = RoutingPriority::NORMAL);
    
    void handle_error(const std::string& error_message, 
                     ipb::common::Result<>::ErrorCode error_code);
    void update_statistics(bool success, std::chrono::nanoseconds duration);
    
    bool should_retry_on_error() const;
    void perform_error_recovery();
    
    // Load balancing helpers
    size_t get_round_robin_index(const std::vector<std::string>& sink_ids);
    size_t get_weighted_round_robin_index(const std::vector<std::string>& sink_ids);
    size_t get_least_connections_index(const std::vector<std::string>& sink_ids);
    size_t get_hash_based_index(const std::vector<std::string>& sink_ids,
                               const ipb::common::DataPoint& data_point);
    
    // Rule validation and optimization
    void optimize_routing_rules();
    void validate_routing_rules();
    
    // Hot reload helpers
    bool check_for_rule_changes();
    ipb::common::Result<> apply_rule_changes();
};

/**
 * @brief Factory for creating routers
 */
class RouterFactory {
public:
    static std::unique_ptr<Router> create(const RouterConfig& config);
    static std::unique_ptr<Router> create_default();
    
    // Preset factories
    static std::unique_ptr<Router> create_high_throughput();
    static std::unique_ptr<Router> create_low_latency();
    static std::unique_ptr<Router> create_realtime();
    static std::unique_ptr<Router> create_reliable();
};

/**
 * @brief Routing rule builder for easy rule construction
 */
class RoutingRuleBuilder {
public:
    RoutingRuleBuilder() = default;
    
    RoutingRuleBuilder& name(const std::string& rule_name);
    RoutingRuleBuilder& priority(RoutingPriority prio);
    RoutingRuleBuilder& enabled(bool is_enabled = true);
    
    // Condition builders
    RoutingRuleBuilder& match_address(const std::string& address);
    RoutingRuleBuilder& match_addresses(const std::vector<std::string>& addresses);
    RoutingRuleBuilder& match_protocol(uint16_t protocol_id);
    RoutingRuleBuilder& match_protocols(const std::vector<uint16_t>& protocol_ids);
    RoutingRuleBuilder& match_pattern(const std::string& regex_pattern);
    RoutingRuleBuilder& match_quality(ipb::common::Quality quality);
    RoutingRuleBuilder& match_time_range(ipb::common::Timestamp start, ipb::common::Timestamp end);
    RoutingRuleBuilder& match_value_condition(const ValueCondition& condition);
    RoutingRuleBuilder& match_custom(std::function<bool(const ipb::common::DataPoint&)> condition);
    
    // Target builders
    RoutingRuleBuilder& route_to(const std::string& sink_id);
    RoutingRuleBuilder& route_to(const std::vector<std::string>& sink_ids);
    RoutingRuleBuilder& load_balance(LoadBalanceStrategy strategy);
    RoutingRuleBuilder& with_weights(const std::vector<uint32_t>& weights);
    RoutingRuleBuilder& with_failover(const std::vector<std::string>& backup_sinks);
    RoutingRuleBuilder& custom_target_selector(
        std::function<std::vector<std::string>(const ipb::common::DataPoint&)> selector);
    
    // Performance builders
    RoutingRuleBuilder& enable_batching(uint32_t batch_size, std::chrono::milliseconds timeout);
    
    RoutingRule build();

private:
    RoutingRule rule_;
    uint32_t rule_id_counter_ = 1;
};

} // namespace ipb::router

