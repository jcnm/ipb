#include "ipb/core/message_bus/channel.hpp"
#include <algorithm>

namespace ipb::core {

// ============================================================================
// Channel Implementation
// ============================================================================

Channel::Channel(std::string topic)
    : topic_(std::move(topic)) {}

Channel::~Channel() = default;

bool Channel::publish(Message msg) {
    msg.sequence = messages_received.fetch_add(1, std::memory_order_relaxed);

    if (!buffer_.try_push(std::move(msg))) {
        messages_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    return true;
}

bool Channel::publish_priority(Message msg, Message::Priority priority) {
    msg.priority = priority;
    return publish(std::move(msg));
}

uint64_t Channel::subscribe(SubscriberCallback callback) {
    uint64_t id = next_subscriber_id_.fetch_add(1, std::memory_order_relaxed);

    std::unique_lock lock(subscribers_mutex_);
    subscribers_.push_back(std::make_unique<SubscriberEntry>(id, std::move(callback)));

    return id;
}

uint64_t Channel::subscribe(SubscriberCallback callback,
                           std::function<bool(const Message&)> filter) {
    uint64_t id = next_subscriber_id_.fetch_add(1, std::memory_order_relaxed);

    std::unique_lock lock(subscribers_mutex_);
    subscribers_.push_back(
        std::make_unique<SubscriberEntry>(id, std::move(callback), std::move(filter)));

    return id;
}

void Channel::unsubscribe(uint64_t subscriber_id) {
    std::unique_lock lock(subscribers_mutex_);

    auto it = std::find_if(subscribers_.begin(), subscribers_.end(),
        [subscriber_id](const auto& entry) {
            return entry->id == subscriber_id;
        });

    if (it != subscribers_.end()) {
        (*it)->active.store(false, std::memory_order_release);
        subscribers_.erase(it);
    }
}

bool Channel::is_subscriber_active(uint64_t subscriber_id) const {
    std::shared_lock lock(subscribers_mutex_);

    auto it = std::find_if(subscribers_.begin(), subscribers_.end(),
        [subscriber_id](const auto& entry) {
            return entry->id == subscriber_id;
        });

    return it != subscribers_.end() && (*it)->active.load(std::memory_order_acquire);
}

size_t Channel::dispatch() {
    size_t count = 0;
    Message msg;

    while (buffer_.try_pop(msg)) {
        dispatch_single(msg);
        ++count;
    }

    return count;
}

void Channel::dispatch_single(const Message& msg) {
    std::shared_lock lock(subscribers_mutex_);

    for (const auto& subscriber : subscribers_) {
        if (!subscriber->active.load(std::memory_order_acquire)) {
            continue;
        }

        // Apply filter if present
        if (subscriber->filter && !subscriber->filter(msg)) {
            continue;
        }

        try {
            subscriber->callback(msg);
        } catch (...) {
            // Subscriber threw exception - continue to other subscribers
        }
    }

    messages_dispatched.fetch_add(1, std::memory_order_relaxed);
}

size_t Channel::pending_count() const noexcept {
    return buffer_.size();
}

size_t Channel::subscriber_count() const noexcept {
    std::shared_lock lock(subscribers_mutex_);
    return subscribers_.size();
}

// ============================================================================
// TopicMatcher Implementation
// ============================================================================

bool TopicMatcher::matches(std::string_view pattern, std::string_view topic) noexcept {
    // Exact match fast path
    if (pattern == topic) {
        return true;
    }

    // No wildcards - must be exact match
    if (!has_wildcards(pattern)) {
        return false;
    }

    size_t pi = 0;  // pattern index
    size_t ti = 0;  // topic index

    while (pi < pattern.size() && ti < topic.size()) {
        if (pattern[pi] == '#') {
            // Multi-level wildcard - matches everything remaining
            return true;
        }

        if (pattern[pi] == '*') {
            // Single-level wildcard - match until next separator
            while (ti < topic.size() && topic[ti] != '/') {
                ++ti;
            }
            ++pi;

            // If pattern has more after *, expect separator
            if (pi < pattern.size()) {
                if (pattern[pi] != '/') {
                    return false;
                }
                ++pi;
                if (ti < topic.size()) {
                    if (topic[ti] != '/') {
                        return false;
                    }
                    ++ti;
                }
            }
            continue;
        }

        // Regular character comparison
        if (pattern[pi] != topic[ti]) {
            return false;
        }

        ++pi;
        ++ti;
    }

    // Handle trailing wildcards
    if (pi < pattern.size() && pattern[pi] == '#') {
        return true;
    }

    // Both must be exhausted for a match
    return pi == pattern.size() && ti == topic.size();
}

bool TopicMatcher::has_wildcards(std::string_view pattern) noexcept {
    return pattern.find_first_of("*#") != std::string_view::npos;
}

bool TopicMatcher::is_valid(std::string_view topic_or_pattern) noexcept {
    if (topic_or_pattern.empty()) {
        return false;
    }

    // Check for invalid characters or sequences
    bool prev_was_separator = true;  // Allow starting without /

    for (size_t i = 0; i < topic_or_pattern.size(); ++i) {
        char c = topic_or_pattern[i];

        if (c == '/') {
            if (prev_was_separator) {
                return false;  // Empty segment
            }
            prev_was_separator = true;
            continue;
        }

        if (c == '#') {
            // # must be last character and at segment start
            if (!prev_was_separator || i != topic_or_pattern.size() - 1) {
                return false;
            }
        }

        if (c == '*') {
            // * must be alone in segment
            if (!prev_was_separator) {
                return false;
            }
            if (i + 1 < topic_or_pattern.size() && topic_or_pattern[i + 1] != '/') {
                return false;
            }
        }

        prev_was_separator = false;
    }

    return true;
}

} // namespace ipb::core
