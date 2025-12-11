#include "ipb/core/message_bus/message_bus.hpp"
#include "ipb/core/message_bus/channel.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace ipb::core {

// ============================================================================
// Subscription Implementation
// ============================================================================

Subscription::Subscription(uint64_t id, std::weak_ptr<Channel> channel)
    : id_(id), channel_(std::move(channel)) {}

Subscription::~Subscription() {
    cancel();
}

bool Subscription::is_active() const noexcept {
    auto channel = channel_.lock();
    return channel && channel->is_subscriber_active(id_);
}

void Subscription::cancel() {
    if (auto channel = channel_.lock()) {
        channel->unsubscribe(id_);
    }
    id_ = 0;
}

// ============================================================================
// MessageBusImpl - Private Implementation
// ============================================================================

class MessageBusImpl {
public:
    explicit MessageBusImpl(const MessageBusConfig& config)
        : config_(config) {
        if (config_.dispatcher_threads == 0) {
            config_.dispatcher_threads = std::thread::hardware_concurrency();
        }
    }

    ~MessageBusImpl() {
        stop();
    }

    bool start() {
        if (running_.exchange(true)) {
            return false;  // Already running
        }

        stop_requested_.store(false);

        // Start dispatcher threads
        for (size_t i = 0; i < config_.dispatcher_threads; ++i) {
            dispatcher_threads_.emplace_back([this, i]() {
                dispatcher_loop(i);
            });

            // Set CPU affinity if configured
            if (config_.cpu_affinity >= 0) {
                common::rt::CPUAffinity::set_thread_affinity(
                    dispatcher_threads_.back().get_id(),
                    config_.cpu_affinity + static_cast<int>(i));
            }

            // Set real-time priority if configured
            if (config_.realtime_priority > 0) {
                common::rt::ThreadPriority::set_realtime_priority(
                    dispatcher_threads_.back().get_id(),
                    config_.realtime_priority);
            }
        }

        return true;
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;  // Not running
        }

        stop_requested_.store(true);
        dispatch_cv_.notify_all();

        for (auto& thread : dispatcher_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        dispatcher_threads_.clear();
    }

    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    bool publish(std::string_view topic, Message msg) {
        auto channel = get_or_create_channel(topic);
        if (!channel) {
            stats_.messages_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        msg.topic = std::string(topic);
        bool success = channel->publish(std::move(msg));

        if (success) {
            stats_.messages_published.fetch_add(1, std::memory_order_relaxed);
            dispatch_cv_.notify_one();
        } else {
            stats_.messages_dropped.fetch_add(1, std::memory_order_relaxed);
            stats_.queue_overflows.fetch_add(1, std::memory_order_relaxed);
        }

        return success;
    }

    bool publish(std::string_view topic, const common::DataPoint& data_point) {
        Message msg(data_point);
        return publish(topic, std::move(msg));
    }

    bool publish_batch(std::string_view topic, std::span<const common::DataPoint> batch) {
        Message msg;
        msg.type = Message::Type::DATA_BATCH;
        msg.batch_payload.assign(batch.begin(), batch.end());
        return publish(topic, std::move(msg));
    }

    bool publish_priority(std::string_view topic, Message msg, Message::Priority priority) {
        msg.priority = priority;
        return publish(topic, std::move(msg));
    }

    bool publish_deadline(std::string_view topic, Message msg, common::Timestamp deadline) {
        msg.type = Message::Type::DEADLINE_TASK;
        msg.deadline_ns = deadline.nanoseconds();
        return publish(topic, std::move(msg));
    }

    Subscription subscribe(std::string_view topic_pattern, SubscriberCallback callback) {
        // For wildcard patterns, we need to track them separately
        if (TopicMatcher::has_wildcards(topic_pattern)) {
            return subscribe_wildcard(topic_pattern, std::move(callback), nullptr);
        }

        auto channel = get_or_create_channel(topic_pattern);
        if (!channel) {
            return Subscription();
        }

        uint64_t id = channel->subscribe(std::move(callback));
        stats_.active_subscriptions.fetch_add(1, std::memory_order_relaxed);

        return Subscription(id, channel);
    }

    Subscription subscribe_filtered(
            std::string_view topic_pattern,
            std::function<bool(const Message&)> filter,
            SubscriberCallback callback) {
        if (TopicMatcher::has_wildcards(topic_pattern)) {
            return subscribe_wildcard(topic_pattern, std::move(callback), std::move(filter));
        }

        auto channel = get_or_create_channel(topic_pattern);
        if (!channel) {
            return Subscription();
        }

        uint64_t id = channel->subscribe(std::move(callback), std::move(filter));
        stats_.active_subscriptions.fetch_add(1, std::memory_order_relaxed);

        return Subscription(id, channel);
    }

    std::shared_ptr<Channel> get_or_create_channel(std::string_view topic) {
        std::string topic_str(topic);

        // Fast path - read-only check
        {
            std::shared_lock lock(channels_mutex_);
            auto it = channels_.find(topic_str);
            if (it != channels_.end()) {
                return it->second;
            }
        }

        // Slow path - create new channel
        std::unique_lock lock(channels_mutex_);

        // Double-check after acquiring write lock
        auto it = channels_.find(topic_str);
        if (it != channels_.end()) {
            return it->second;
        }

        // Check capacity
        if (channels_.size() >= config_.max_channels) {
            return nullptr;
        }

        auto channel = std::make_shared<Channel>(topic_str);
        channels_[topic_str] = channel;
        stats_.active_channels.fetch_add(1, std::memory_order_relaxed);

        return channel;
    }

    bool has_channel(std::string_view topic) const {
        std::shared_lock lock(channels_mutex_);
        return channels_.find(std::string(topic)) != channels_.end();
    }

    std::vector<std::string> get_topics() const {
        std::shared_lock lock(channels_mutex_);
        std::vector<std::string> topics;
        topics.reserve(channels_.size());

        for (const auto& [topic, _] : channels_) {
            topics.push_back(topic);
        }

        return topics;
    }

    const MessageBusStats& stats() const noexcept {
        return stats_;
    }

    void reset_stats() {
        stats_.reset();
    }

    const MessageBusConfig& config() const noexcept {
        return config_;
    }

private:
    void dispatcher_loop(size_t thread_id) {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            size_t total_dispatched = 0;

            // Dispatch from all channels
            {
                std::shared_lock lock(channels_mutex_);
                for (auto& [_, channel] : channels_) {
                    total_dispatched += channel->dispatch();
                }
            }

            // Dispatch to wildcard subscribers
            dispatch_wildcard_subscriptions();

            if (total_dispatched > 0) {
                stats_.messages_delivered.fetch_add(total_dispatched, std::memory_order_relaxed);
            } else {
                // No messages - wait for notification
                std::unique_lock lock(dispatch_mutex_);
                dispatch_cv_.wait_for(lock, std::chrono::microseconds(100));
            }
        }
    }

    Subscription subscribe_wildcard(
            std::string_view pattern,
            SubscriberCallback callback,
            std::function<bool(const Message&)> filter) {
        uint64_t id = next_wildcard_id_.fetch_add(1, std::memory_order_relaxed);

        std::unique_lock lock(wildcards_mutex_);
        wildcard_subscriptions_.push_back({
            id,
            std::string(pattern),
            std::move(callback),
            std::move(filter)
        });

        stats_.active_subscriptions.fetch_add(1, std::memory_order_relaxed);

        // Return a dummy subscription - wildcard subs are managed separately
        return Subscription(id, std::weak_ptr<Channel>());
    }

    void dispatch_wildcard_subscriptions() {
        std::shared_lock channels_lock(channels_mutex_);
        std::shared_lock wildcards_lock(wildcards_mutex_);

        for (const auto& sub : wildcard_subscriptions_) {
            for (const auto& [topic, channel] : channels_) {
                if (TopicMatcher::matches(sub.pattern, topic)) {
                    // This channel matches the wildcard pattern
                    // Dispatch any pending messages through the wildcard callback
                    Message msg;
                    // Note: We can't pop from channel without modifying it
                    // This is a simplified implementation - real impl would need
                    // to handle this differently (e.g., broadcast channels)
                }
            }
        }
    }

    MessageBusConfig config_;
    MessageBusStats stats_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    // Channel storage
    mutable std::shared_mutex channels_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Channel>> channels_;

    // Wildcard subscriptions
    struct WildcardSub {
        uint64_t id;
        std::string pattern;
        SubscriberCallback callback;
        std::function<bool(const Message&)> filter;
    };
    mutable std::shared_mutex wildcards_mutex_;
    std::vector<WildcardSub> wildcard_subscriptions_;
    std::atomic<uint64_t> next_wildcard_id_{1};

    // Dispatcher threads
    std::vector<std::thread> dispatcher_threads_;
    std::mutex dispatch_mutex_;
    std::condition_variable dispatch_cv_;
};

// ============================================================================
// MessageBus Public Interface
// ============================================================================

MessageBus::MessageBus()
    : impl_(std::make_unique<MessageBusImpl>(MessageBusConfig{})) {}

MessageBus::MessageBus(const MessageBusConfig& config)
    : impl_(std::make_unique<MessageBusImpl>(config)) {}

MessageBus::~MessageBus() = default;

MessageBus::MessageBus(MessageBus&&) noexcept = default;
MessageBus& MessageBus::operator=(MessageBus&&) noexcept = default;

bool MessageBus::start() {
    return impl_->start();
}

void MessageBus::stop() {
    impl_->stop();
}

bool MessageBus::is_running() const noexcept {
    return impl_->is_running();
}

bool MessageBus::publish(std::string_view topic, Message msg) {
    return impl_->publish(topic, std::move(msg));
}

bool MessageBus::publish(std::string_view topic, const common::DataPoint& data_point) {
    return impl_->publish(topic, data_point);
}

bool MessageBus::publish_batch(std::string_view topic, std::span<const common::DataPoint> batch) {
    return impl_->publish_batch(topic, batch);
}

bool MessageBus::publish_priority(std::string_view topic, Message msg, Message::Priority priority) {
    return impl_->publish_priority(topic, std::move(msg), priority);
}

bool MessageBus::publish_deadline(std::string_view topic, Message msg, common::Timestamp deadline) {
    return impl_->publish_deadline(topic, std::move(msg), deadline);
}

Subscription MessageBus::subscribe(std::string_view topic_pattern, SubscriberCallback callback) {
    return impl_->subscribe(topic_pattern, std::move(callback));
}

Subscription MessageBus::subscribe_filtered(
        std::string_view topic_pattern,
        std::function<bool(const Message&)> filter,
        SubscriberCallback callback) {
    return impl_->subscribe_filtered(topic_pattern, std::move(filter), std::move(callback));
}

std::shared_ptr<Channel> MessageBus::get_or_create_channel(std::string_view topic) {
    return impl_->get_or_create_channel(topic);
}

bool MessageBus::has_channel(std::string_view topic) const {
    return impl_->has_channel(topic);
}

std::vector<std::string> MessageBus::get_topics() const {
    return impl_->get_topics();
}

const MessageBusStats& MessageBus::stats() const noexcept {
    return impl_->stats();
}

void MessageBus::reset_stats() {
    impl_->reset_stats();
}

const MessageBusConfig& MessageBus::config() const noexcept {
    return impl_->config();
}

} // namespace ipb::core
