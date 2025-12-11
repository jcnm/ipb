#pragma once

/**
 * @file message_bus.hpp
 * @brief Lock-free message bus for high-performance pub/sub communication
 *
 * The MessageBus provides a decoupled communication mechanism between
 * IPB components with the following characteristics:
 * - Lock-free MPMC (Multi-Producer Multi-Consumer) channels
 * - Topic-based message routing
 * - Zero-copy message passing where possible
 * - Real-time safe operations (no allocations in hot path)
 * - Target throughput: >5M messages/second
 */

#include <ipb/common/data_point.hpp>
#include <ipb/common/endpoint.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ipb::core {

// Forward declarations
class Channel;
class MessageBusImpl;

/**
 * @brief Message envelope for bus transport
 */
struct Message {
    /// Message types for different routing behaviors
    enum class Type : uint8_t {
        DATA_POINT,      ///< Single data point
        DATA_BATCH,      ///< Batch of data points
        CONTROL,         ///< Control message (start/stop/config)
        HEARTBEAT,       ///< Health check message
        DEADLINE_TASK    ///< Task with deadline for EDF scheduler
    };

    /// Message priority levels
    enum class Priority : uint8_t {
        LOW = 0,
        NORMAL = 64,
        HIGH = 128,
        REALTIME = 255
    };

    Type type = Type::DATA_POINT;
    Priority priority = Priority::NORMAL;

    /// Source identifier
    std::string source_id;

    /// Topic for routing
    std::string topic;

    /// Payload (DataPoint for most messages)
    common::DataPoint payload;

    /// Batch payload for DATA_BATCH type
    std::vector<common::DataPoint> batch_payload;

    /// Deadline for DEADLINE_TASK (nanoseconds since epoch)
    int64_t deadline_ns = 0;

    /// Sequence number for ordering
    uint64_t sequence = 0;

    /// Creation timestamp
    common::Timestamp timestamp;

    Message() : timestamp(common::Timestamp::now()) {}

    explicit Message(common::DataPoint dp)
        : type(Type::DATA_POINT)
        , payload(std::move(dp))
        , timestamp(common::Timestamp::now()) {}

    Message(std::string_view topic_name, common::DataPoint dp)
        : type(Type::DATA_POINT)
        , topic(topic_name)
        , payload(std::move(dp))
        , timestamp(common::Timestamp::now()) {}
};

/**
 * @brief Subscriber callback signature
 */
using SubscriberCallback = std::function<void(const Message&)>;

/**
 * @brief Subscription handle for managing subscriptions
 */
class Subscription {
public:
    Subscription() = default;
    Subscription(uint64_t id, std::weak_ptr<Channel> channel);

    Subscription(Subscription&&) noexcept = default;
    Subscription& operator=(Subscription&&) noexcept = default;

    // Non-copyable
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    ~Subscription();

    /// Check if subscription is active
    bool is_active() const noexcept;

    /// Cancel the subscription
    void cancel();

    /// Get subscription ID
    uint64_t id() const noexcept { return id_; }

private:
    uint64_t id_ = 0;
    std::weak_ptr<Channel> channel_;
};

/**
 * @brief Statistics for message bus monitoring
 */
struct MessageBusStats {
    std::atomic<uint64_t> messages_published{0};
    std::atomic<uint64_t> messages_delivered{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> queue_overflows{0};

    std::atomic<uint64_t> active_subscriptions{0};
    std::atomic<uint64_t> active_channels{0};

    std::atomic<int64_t> min_latency_ns{INT64_MAX};
    std::atomic<int64_t> max_latency_ns{0};
    std::atomic<int64_t> total_latency_ns{0};

    /// Calculate messages per second
    double messages_per_second(std::chrono::nanoseconds elapsed) const noexcept {
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        return seconds > 0 ? static_cast<double>(messages_published.load()) / seconds : 0.0;
    }

    /// Calculate average latency
    double avg_latency_us() const noexcept {
        auto count = messages_delivered.load();
        return count > 0 ? static_cast<double>(total_latency_ns.load()) / count / 1000.0 : 0.0;
    }

    void reset() noexcept {
        messages_published.store(0);
        messages_delivered.store(0);
        messages_dropped.store(0);
        queue_overflows.store(0);
        min_latency_ns.store(INT64_MAX);
        max_latency_ns.store(0);
        total_latency_ns.store(0);
    }
};

/**
 * @brief Configuration for MessageBus
 */
struct MessageBusConfig {
    /// Maximum number of channels
    size_t max_channels = 256;

    /// Default channel buffer size (must be power of 2)
    size_t default_buffer_size = 65536;

    /// Number of dispatcher threads (0 = use hardware concurrency)
    size_t dispatcher_threads = 0;

    /// Enable lock-free mode (requires careful memory management)
    bool lock_free_mode = true;

    /// Enable priority-based dispatch
    bool priority_dispatch = true;

    /// Drop policy when buffer is full
    enum class DropPolicy {
        DROP_NEWEST,    ///< Drop incoming messages
        DROP_OLDEST,    ///< Drop oldest messages in queue
        BLOCK           ///< Block publisher (NOT real-time safe!)
    } drop_policy = DropPolicy::DROP_OLDEST;

    /// CPU affinity for dispatcher threads (-1 = no affinity)
    int cpu_affinity = -1;

    /// Real-time priority for dispatcher threads (0 = normal)
    int realtime_priority = 0;
};

/**
 * @brief High-performance message bus for component communication
 *
 * The MessageBus is the central nervous system of IPB, providing:
 * - Topic-based pub/sub messaging
 * - Lock-free message passing
 * - Priority-based dispatch
 * - Zero-copy where possible
 *
 * Example usage:
 * @code
 * MessageBus bus;
 * bus.start();
 *
 * // Subscribe to a topic
 * auto sub = bus.subscribe("sensors/*", [](const Message& msg) {
 *     // Handle message
 * });
 *
 * // Publish a message
 * bus.publish("sensors/temp1", DataPoint("temp1", Value{25.5}));
 * @endcode
 */
class MessageBus {
public:
    MessageBus();
    explicit MessageBus(const MessageBusConfig& config);
    ~MessageBus();

    // Non-copyable, movable
    MessageBus(const MessageBus&) = delete;
    MessageBus& operator=(const MessageBus&) = delete;
    MessageBus(MessageBus&&) noexcept;
    MessageBus& operator=(MessageBus&&) noexcept;

    // Lifecycle

    /// Start the message bus
    bool start();

    /// Stop the message bus
    void stop();

    /// Check if running
    bool is_running() const noexcept;

    // Publishing

    /// Publish a message to a topic
    bool publish(std::string_view topic, Message msg);

    /// Publish a data point to a topic
    bool publish(std::string_view topic, const common::DataPoint& data_point);

    /// Publish a batch of data points
    bool publish_batch(std::string_view topic, std::span<const common::DataPoint> batch);

    /// Publish with priority
    bool publish_priority(std::string_view topic, Message msg, Message::Priority priority);

    /// Publish with deadline (for EDF scheduler)
    bool publish_deadline(std::string_view topic, Message msg, common::Timestamp deadline);

    // Subscribing

    /// Subscribe to a topic pattern (supports wildcards: * and #)
    [[nodiscard]] Subscription subscribe(std::string_view topic_pattern, SubscriberCallback callback);

    /// Subscribe with filter predicate
    [[nodiscard]] Subscription subscribe_filtered(
        std::string_view topic_pattern,
        std::function<bool(const Message&)> filter,
        SubscriberCallback callback);

    // Channel management

    /// Create or get a channel for a topic
    std::shared_ptr<Channel> get_or_create_channel(std::string_view topic);

    /// Check if a channel exists
    bool has_channel(std::string_view topic) const;

    /// Get all active topics
    std::vector<std::string> get_topics() const;

    // Statistics

    /// Get current statistics
    const MessageBusStats& stats() const noexcept;

    /// Reset statistics
    void reset_stats();

    // Configuration

    /// Get current configuration
    const MessageBusConfig& config() const noexcept;

private:
    std::unique_ptr<MessageBusImpl> impl_;
};

} // namespace ipb::core
