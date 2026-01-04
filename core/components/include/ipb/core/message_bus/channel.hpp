#pragma once

// MSVC: Disable C4324 warning for intentional cache-line padding
// This warning is expected as we deliberately use alignas() to prevent false sharing
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // structure was padded due to alignment specifier
#endif

/**
 * @file channel.hpp
 * @brief Lock-free MPMC channel for message transport
 *
 * Implements a high-performance, lock-free multi-producer multi-consumer
 * channel optimized for real-time message passing.
 */

#include <ipb/common/endpoint.hpp>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "message_bus.hpp"

namespace ipb::core {

/**
 * @brief Lock-free MPMC ring buffer for messages
 *
 * Uses a bounded ring buffer with atomic operations for
 * lock-free producer/consumer synchronization.
 */
template <size_t Capacity>
class MPMCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    MPMCRingBuffer() noexcept {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Try to push a message (non-blocking)
     * @return true if successful, false if buffer full
     */
    bool try_push(Message&& msg) noexcept {
        Slot* slot;
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            slot          = &slots_[pos & (Capacity - 1)];
            size_t seq    = slot->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // Buffer full
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }

        slot->message = std::move(msg);
        slot->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Try to pop a message (non-blocking)
     * @return true if successful, false if buffer empty
     */
    bool try_pop(Message& msg) noexcept {
        Slot* slot;
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            slot          = &slots_[pos & (Capacity - 1)];
            size_t seq    = slot->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // Buffer empty
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

        msg = std::move(slot->message);
        slot->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    /**
     * @brief Get approximate size
     */
    size_t size() const noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);
        return head >= tail ? head - tail : 0;
    }

    bool empty() const noexcept { return size() == 0; }
    bool full() const noexcept { return size() >= Capacity - 1; }
    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    struct alignas(64) Slot {
        std::atomic<size_t> sequence;
        Message message;
    };

    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    std::array<Slot, Capacity> slots_;
};

/**
 * @brief Subscriber entry with callback and filter
 */
struct SubscriberEntry {
    uint64_t id;
    SubscriberCallback callback;
    std::function<bool(const Message&)> filter;
    std::atomic<bool> active{true};

    SubscriberEntry(uint64_t subscriber_id, SubscriberCallback cb)
        : id(subscriber_id), callback(std::move(cb)) {}

    SubscriberEntry(uint64_t subscriber_id, SubscriberCallback cb,
                    std::function<bool(const Message&)> flt)
        : id(subscriber_id), callback(std::move(cb)), filter(std::move(flt)) {}
};

/**
 * @brief Message channel for topic-based routing
 *
 * Each channel handles messages for a specific topic pattern.
 * Channels maintain their own buffer and subscriber list.
 */
class Channel : public std::enable_shared_from_this<Channel> {
public:
    /// Default buffer capacity (64K messages)
    static constexpr size_t DEFAULT_CAPACITY = 65536;

    explicit Channel(std::string topic);
    ~Channel();

    // Non-copyable
    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    /// Get topic name
    const std::string& topic() const noexcept { return topic_; }

    // Publishing

    /// Publish a message to this channel
    bool publish(Message msg);

    /// Publish with priority override
    bool publish_priority(Message msg, Message::Priority priority);

    // Subscribing

    /// Add a subscriber
    uint64_t subscribe(SubscriberCallback callback);

    /// Add a subscriber with filter
    uint64_t subscribe(SubscriberCallback callback, std::function<bool(const Message&)> filter);

    /// Remove a subscriber
    void unsubscribe(uint64_t subscriber_id);

    /// Check if subscriber is active
    bool is_subscriber_active(uint64_t subscriber_id) const;

    // Dispatch

    /// Dispatch pending messages to subscribers
    size_t dispatch();

    /// Dispatch a single message
    void dispatch_single(const Message& msg);

    // Status

    /// Get number of pending messages
    size_t pending_count() const noexcept;

    /// Get number of subscribers
    size_t subscriber_count() const noexcept;

    /// Check if channel is empty
    bool empty() const noexcept { return pending_count() == 0; }

    // Statistics

    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_dispatched{0};
    std::atomic<uint64_t> messages_dropped{0};

private:
    std::string topic_;

    // Lock-free message buffer
    MPMCRingBuffer<DEFAULT_CAPACITY> buffer_;

    // Subscriber list (protected by shared mutex for rare modifications)
    mutable std::shared_mutex subscribers_mutex_;
    std::vector<std::unique_ptr<SubscriberEntry>> subscribers_;
    std::atomic<uint64_t> next_subscriber_id_{1};
};

/**
 * @brief Topic pattern matcher for wildcard subscriptions
 *
 * Supports:
 * - Exact matching: "sensors/temp1"
 * - Single-level wildcard (*): "sensors/+" matches "sensors/temp1"
 * - Multi-level wildcard (#): "sensors/#" matches "sensors/temp1/value"
 */
class TopicMatcher {
public:
    /// Check if a topic matches a pattern
    static bool matches(std::string_view pattern, std::string_view topic) noexcept;

    /// Check if pattern contains wildcards
    static bool has_wildcards(std::string_view pattern) noexcept;

    /// Validate topic or pattern syntax
    static bool is_valid(std::string_view topic_or_pattern) noexcept;
};

}  // namespace ipb::core

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
