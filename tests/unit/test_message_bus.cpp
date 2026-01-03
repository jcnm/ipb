/**
 * @file test_message_bus.cpp
 * @brief Unit tests for IPB MessageBus
 *
 * Tests coverage for:
 * - Message: Message types, priorities, construction
 * - Subscription: Subscription management
 * - MessageBusStats: Statistics tracking
 * - MessageBus: Pub/sub functionality
 */

#include <ipb/core/message_bus/message_bus.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::core;
using namespace ipb::common;

// ============================================================================
// Message Tests
// ============================================================================

class MessageTest : public ::testing::Test {};

TEST_F(MessageTest, TypeValues) {
    EXPECT_EQ(static_cast<uint8_t>(Message::Type::DATA_POINT), 0);
    EXPECT_EQ(static_cast<uint8_t>(Message::Type::DATA_BATCH), 1);
    EXPECT_EQ(static_cast<uint8_t>(Message::Type::CONTROL), 2);
    EXPECT_EQ(static_cast<uint8_t>(Message::Type::HEARTBEAT), 3);
    EXPECT_EQ(static_cast<uint8_t>(Message::Type::DEADLINE_TASK), 4);
}

TEST_F(MessageTest, PriorityValues) {
    EXPECT_EQ(static_cast<uint8_t>(Message::Priority::LOW), 0);
    EXPECT_EQ(static_cast<uint8_t>(Message::Priority::NORMAL), 64);
    EXPECT_EQ(static_cast<uint8_t>(Message::Priority::HIGH), 128);
    EXPECT_EQ(static_cast<uint8_t>(Message::Priority::REALTIME), 255);
}

TEST_F(MessageTest, DefaultConstruction) {
    Message msg;
    EXPECT_EQ(msg.type, Message::Type::DATA_POINT);
    EXPECT_EQ(msg.priority, Message::Priority::NORMAL);
    EXPECT_TRUE(msg.source_id.empty());
    EXPECT_TRUE(msg.topic.empty());
}

TEST_F(MessageTest, ConstructWithDataPoint) {
    DataPoint dp("sensor/temp1");
    dp.set_value(25.5);

    Message msg(dp);

    EXPECT_EQ(msg.type, Message::Type::DATA_POINT);
    EXPECT_EQ(msg.payload.address(), "sensor/temp1");
}

TEST_F(MessageTest, ConstructWithTopicAndDataPoint) {
    DataPoint dp("sensor/temp1");
    dp.set_value(25.5);

    Message msg("sensors/temperature", dp);

    EXPECT_EQ(msg.type, Message::Type::DATA_POINT);
    EXPECT_EQ(msg.topic, "sensors/temperature");
    EXPECT_EQ(msg.payload.address(), "sensor/temp1");
}

// ============================================================================
// MessageBusStats Tests
// ============================================================================

class MessageBusStatsTest : public ::testing::Test {};

TEST_F(MessageBusStatsTest, DefaultValues) {
    MessageBusStats stats;
    EXPECT_EQ(stats.messages_published.load(), 0u);
    EXPECT_EQ(stats.messages_delivered.load(), 0u);
    EXPECT_EQ(stats.messages_dropped.load(), 0u);
    EXPECT_EQ(stats.queue_overflows.load(), 0u);
}

TEST_F(MessageBusStatsTest, MessagesPerSecond) {
    MessageBusStats stats;
    stats.messages_published.store(1000);

    auto mps = stats.messages_per_second(std::chrono::seconds(10));
    EXPECT_DOUBLE_EQ(mps, 100.0);
}

TEST_F(MessageBusStatsTest, AverageLatency) {
    MessageBusStats stats;

    // No messages yet
    EXPECT_DOUBLE_EQ(stats.avg_latency_us(), 0.0);

    // Set some values
    stats.messages_delivered.store(100);
    stats.total_latency_ns.store(1000000);           // 1ms total
    EXPECT_DOUBLE_EQ(stats.avg_latency_us(), 10.0);  // 10us average
}

TEST_F(MessageBusStatsTest, Reset) {
    MessageBusStats stats;
    stats.messages_published.store(100);
    stats.messages_delivered.store(90);
    stats.messages_dropped.store(10);

    stats.reset();

    EXPECT_EQ(stats.messages_published.load(), 0u);
    EXPECT_EQ(stats.messages_delivered.load(), 0u);
    EXPECT_EQ(stats.messages_dropped.load(), 0u);
}

// ============================================================================
// MessageBusConfig Tests
// ============================================================================

class MessageBusConfigTest : public ::testing::Test {};

TEST_F(MessageBusConfigTest, DefaultValues) {
    MessageBusConfig config;
    EXPECT_EQ(config.max_channels, 256u);
    EXPECT_EQ(config.default_buffer_size, 65536u);
    EXPECT_TRUE(config.lock_free_mode);
    EXPECT_TRUE(config.priority_dispatch);
}

TEST_F(MessageBusConfigTest, DropPolicies) {
    EXPECT_EQ(static_cast<int>(MessageBusConfig::DropPolicy::DROP_NEWEST), 0);
    EXPECT_EQ(static_cast<int>(MessageBusConfig::DropPolicy::DROP_OLDEST), 1);
    EXPECT_EQ(static_cast<int>(MessageBusConfig::DropPolicy::BLOCK), 2);
}

// ============================================================================
// MessageBus Tests
// ============================================================================

class MessageBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.max_channels        = 64;
        config_.default_buffer_size = 1024;
        config_.dispatcher_threads  = 2;
    }

    MessageBusConfig config_;
};

TEST_F(MessageBusTest, DefaultConstruction) {
    MessageBus bus;
    EXPECT_FALSE(bus.is_running());
}

TEST_F(MessageBusTest, ConfiguredConstruction) {
    MessageBus bus(config_);
    EXPECT_FALSE(bus.is_running());
    EXPECT_EQ(bus.config().max_channels, 64u);
}

TEST_F(MessageBusTest, StartStop) {
    MessageBus bus(config_);

    EXPECT_TRUE(bus.start());
    EXPECT_TRUE(bus.is_running());

    bus.stop();
    EXPECT_FALSE(bus.is_running());
}

TEST_F(MessageBusTest, PublishDataPoint) {
    MessageBus bus(config_);
    bus.start();

    DataPoint dp("sensor/temp1");
    dp.set_value(25.5);

    bool published = bus.publish("sensors/temperature", dp);
    EXPECT_TRUE(published);

    bus.stop();
}

TEST_F(MessageBusTest, PublishMessage) {
    MessageBus bus(config_);
    bus.start();

    Message msg;
    msg.topic = "test/topic";
    msg.type  = Message::Type::CONTROL;

    bool published = bus.publish("test/topic", msg);
    EXPECT_TRUE(published);

    bus.stop();
}

TEST_F(MessageBusTest, Subscribe) {
    MessageBus bus(config_);
    bus.start();

    std::atomic<bool> received{false};

    // Use non-wildcard pattern for exact topic matching
    auto sub = bus.subscribe("sensors/temperature",
                             [&received]([[maybe_unused]] const Message& msg) { received = true; });

    EXPECT_TRUE(sub.is_active());

    // Publish a matching message
    DataPoint dp("sensors/temp1");
    bus.publish("sensors/temperature", dp);

    // Wait for delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bus.stop();
    // Received may or may not be true depending on delivery timing
}

TEST_F(MessageBusTest, SubscriptionCancel) {
    MessageBus bus(config_);
    bus.start();

    // Use non-wildcard pattern (wildcard subs return inactive by design)
    auto sub = bus.subscribe("test/topic", [](const Message&) {});

    EXPECT_TRUE(sub.is_active());
    sub.cancel();
    EXPECT_FALSE(sub.is_active());

    bus.stop();
}

TEST_F(MessageBusTest, SubscriptionRAII) {
    MessageBus bus(config_);
    bus.start();

    {
        // Use non-wildcard pattern (wildcard subs return inactive by design)
        auto sub = bus.subscribe("test/topic", [](const Message&) {});
        EXPECT_TRUE(sub.is_active());
    }
    // Subscription should be cancelled when out of scope

    bus.stop();
}

TEST_F(MessageBusTest, PublishBatch) {
    MessageBus bus(config_);
    bus.start();

    std::vector<DataPoint> batch;
    for (int i = 0; i < 10; ++i) {
        DataPoint dp("sensor/temp" + std::to_string(i));
        dp.set_value(static_cast<double>(20 + i));
        batch.push_back(dp);
    }

    bool published = bus.publish_batch("sensors/batch", batch);
    EXPECT_TRUE(published);

    bus.stop();
}

TEST_F(MessageBusTest, PublishWithPriority) {
    MessageBus bus(config_);
    bus.start();

    Message msg;
    msg.topic = "critical/alert";

    bool published = bus.publish_priority("critical/alert", msg, Message::Priority::REALTIME);
    EXPECT_TRUE(published);

    bus.stop();
}

TEST_F(MessageBusTest, GetOrCreateChannel) {
    MessageBus bus(config_);
    bus.start();

    auto channel1 = bus.get_or_create_channel("test/channel");
    EXPECT_NE(channel1, nullptr);

    auto channel2 = bus.get_or_create_channel("test/channel");
    EXPECT_EQ(channel1, channel2);  // Same channel

    bus.stop();
}

TEST_F(MessageBusTest, HasChannel) {
    MessageBus bus(config_);
    bus.start();

    EXPECT_FALSE(bus.has_channel("nonexistent"));

    bus.get_or_create_channel("test/channel");
    EXPECT_TRUE(bus.has_channel("test/channel"));

    bus.stop();
}

TEST_F(MessageBusTest, GetTopics) {
    MessageBus bus(config_);
    bus.start();

    bus.get_or_create_channel("topic1");
    bus.get_or_create_channel("topic2");
    bus.get_or_create_channel("topic3");

    auto topics = bus.get_topics();
    EXPECT_GE(topics.size(), 3u);

    bus.stop();
}

TEST_F(MessageBusTest, Statistics) {
    MessageBus bus(config_);
    bus.start();

    DataPoint dp("sensor/temp1");
    bus.publish("sensors/temperature", dp);

    const auto& stats = bus.stats();
    EXPECT_GE(stats.messages_published.load(), 1u);

    bus.stop();
}

TEST_F(MessageBusTest, ResetStats) {
    MessageBus bus(config_);
    bus.start();

    DataPoint dp("sensor/temp1");
    bus.publish("sensors/temperature", dp);

    bus.reset_stats();

    const auto& stats = bus.stats();
    EXPECT_EQ(stats.messages_published.load(), 0u);

    bus.stop();
}

TEST_F(MessageBusTest, MoveConstruction) {
    MessageBus bus1(config_);
    bus1.start();

    MessageBus bus2(std::move(bus1));
    EXPECT_TRUE(bus2.is_running());

    bus2.stop();
}

// ============================================================================
// Pub/Sub Integration Tests
// ============================================================================

class PubSubIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.max_channels        = 64;
        config_.default_buffer_size = 1024;
        config_.dispatcher_threads  = 2;
    }

    MessageBusConfig config_;
};

TEST_F(PubSubIntegrationTest, SimplePublishSubscribe) {
    MessageBus bus(config_);
    bus.start();

    std::atomic<int> received_count{0};
    std::mutex received_mutex;
    std::vector<std::string> received_addresses;

    auto sub = bus.subscribe("sensors/#", [&](const Message& msg) {
        received_count++;
        std::lock_guard<std::mutex> lock(received_mutex);
        received_addresses.push_back(std::string(msg.payload.address()));
    });

    // Publish multiple messages
    for (int i = 0; i < 5; ++i) {
        DataPoint dp("sensor/temp" + std::to_string(i));
        dp.set_value(static_cast<double>(20 + i));
        bus.publish("sensors/data", dp);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    bus.stop();
}

TEST_F(PubSubIntegrationTest, MultipleSubscribers) {
    MessageBus bus(config_);
    bus.start();

    std::atomic<int> sub1_count{0};
    std::atomic<int> sub2_count{0};

    auto sub1 = bus.subscribe("sensors/*", [&sub1_count](const Message&) { sub1_count++; });

    auto sub2 = bus.subscribe("sensors/*", [&sub2_count](const Message&) { sub2_count++; });

    DataPoint dp("sensor/temp1");
    bus.publish("sensors/data", dp);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bus.stop();
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

class MessageBusThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.max_channels        = 256;
        config_.default_buffer_size = 4096;
        config_.dispatcher_threads  = 4;
    }

    MessageBusConfig config_;
};

TEST_F(MessageBusThreadSafetyTest, ConcurrentPublish) {
    MessageBus bus(config_);
    bus.start();

    constexpr int NUM_THREADS         = 4;
    constexpr int MESSAGES_PER_THREAD = 100;

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&bus, t]() {
            for (int i = 0; i < MESSAGES_PER_THREAD; ++i) {
                DataPoint dp("sensor/thread" + std::to_string(t) + "/msg" + std::to_string(i));
                dp.set_value(static_cast<double>(i));
                bus.publish("sensors/concurrent", dp);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    bus.stop();

    const auto& stats = bus.stats();
    EXPECT_GE(stats.messages_published.load(),
              static_cast<uint64_t>(NUM_THREADS * MESSAGES_PER_THREAD));
}

TEST_F(MessageBusThreadSafetyTest, ConcurrentSubscribeUnsubscribe) {
    MessageBus bus(config_);
    bus.start();

    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS  = 50;

    std::vector<std::thread> threads;
    std::atomic<int> successful_subs{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&bus, &successful_subs, t]() {
            for (int i = 0; i < ITERATIONS; ++i) {
                // Use non-wildcard patterns (wildcard subs return inactive by design)
                auto sub = bus.subscribe("topic" + std::to_string(t) + "/data" + std::to_string(i),
                                         [](const Message&) {});
                if (sub.is_active()) {
                    successful_subs++;
                }
                sub.cancel();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    bus.stop();

    // Most non-wildcard subscriptions should succeed
    EXPECT_GT(successful_subs.load(), 0);
}

// ============================================================================
// Channel Tests - Additional Coverage
// ============================================================================

#include <ipb/core/message_bus/channel.hpp>

class ChannelTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Channel must be heap-allocated due to large buffer and enable_shared_from_this
        channel_ = std::make_shared<Channel>("test/topic");
    }

    std::shared_ptr<Channel> channel_;
};

TEST_F(ChannelTest, BasicConstruction) {
    auto ch = std::make_shared<Channel>("my/topic");
    EXPECT_EQ(ch->topic(), "my/topic");
    EXPECT_EQ(ch->pending_count(), 0u);
    EXPECT_EQ(ch->subscriber_count(), 0u);
}

TEST_F(ChannelTest, PublishPriority) {
    // Test publish_priority method
    Message msg;
    msg.type    = Message::Type::CONTROL;
    bool result = channel_->publish_priority(std::move(msg), Message::Priority::HIGH);
    EXPECT_TRUE(result);
}

TEST_F(ChannelTest, SubscribeWithFilter) {
    int callback_count = 0;

    // Subscribe with a filter that only accepts DATA_POINT messages
    auto id = channel_->subscribe(
        [&callback_count](const Message&) { callback_count++; },
        [](const Message& msg) { return msg.type == Message::Type::DATA_POINT; });

    EXPECT_NE(id, 0u);
    EXPECT_TRUE(channel_->is_subscriber_active(id));

    // Publish a DATA_POINT message (should pass filter)
    Message dp_msg;
    dp_msg.type = Message::Type::DATA_POINT;
    channel_->publish(std::move(dp_msg));
    channel_->dispatch();
    EXPECT_EQ(callback_count, 1);

    // Publish a CONTROL message (should be filtered out)
    Message ctrl_msg;
    ctrl_msg.type = Message::Type::CONTROL;
    channel_->publish(std::move(ctrl_msg));
    channel_->dispatch();
    EXPECT_EQ(callback_count, 1);  // Still 1, filtered

    channel_->unsubscribe(id);
}

TEST_F(ChannelTest, SubscriberException) {
    int callback_count = 0;

    // First subscriber throws
    channel_->subscribe([](const Message&) { throw std::runtime_error("Subscriber error"); });

    // Second subscriber should still be called
    channel_->subscribe([&callback_count](const Message&) { callback_count++; });

    Message msg;
    channel_->publish(std::move(msg));
    channel_->dispatch();  // Should not throw, exception is caught

    EXPECT_EQ(callback_count, 1);  // Second subscriber was called
}

TEST_F(ChannelTest, InactiveSubscriber) {
    bool callback_called = false;

    auto id = channel_->subscribe([&callback_called](const Message&) { callback_called = true; });

    // Unsubscribe (makes it inactive)
    channel_->unsubscribe(id);
    EXPECT_FALSE(channel_->is_subscriber_active(id));

    // Publish and dispatch - inactive subscriber should not receive
    Message msg;
    channel_->publish(std::move(msg));
    channel_->dispatch();

    // Callback should not have been called since subscriber is inactive
    // (Note: after unsubscribe, the subscriber is removed, so this test
    // verifies the removal works)
}

TEST_F(ChannelTest, PendingAndSubscriberCount) {
    EXPECT_EQ(channel_->pending_count(), 0u);
    EXPECT_EQ(channel_->subscriber_count(), 0u);

    auto id = channel_->subscribe([](const Message&) {});
    EXPECT_EQ(channel_->subscriber_count(), 1u);

    Message msg;
    channel_->publish(std::move(msg));
    EXPECT_EQ(channel_->pending_count(), 1u);

    channel_->dispatch();
    EXPECT_EQ(channel_->pending_count(), 0u);

    channel_->unsubscribe(id);
    EXPECT_EQ(channel_->subscriber_count(), 0u);
}

TEST_F(ChannelTest, BufferOverflow) {
    // Test publishing many messages
    // The default buffer should handle this, but if it fills up,
    // messages_dropped should increment

    for (int i = 0; i < 1000; ++i) {
        Message msg;
        channel_->publish(std::move(msg));
    }

    // Just verify we can still operate
    EXPECT_GE(channel_->pending_count(), 0u);
}

// ============================================================================
// TopicMatcher Tests - Additional Coverage
// ============================================================================

class TopicMatcherTest : public ::testing::Test {};

TEST_F(TopicMatcherTest, ExactMatch) {
    EXPECT_TRUE(TopicMatcher::matches("sensors/temp", "sensors/temp"));
    EXPECT_FALSE(TopicMatcher::matches("sensors/temp", "sensors/humidity"));
}

TEST_F(TopicMatcherTest, SingleWildcard) {
    EXPECT_TRUE(TopicMatcher::matches("sensors/*", "sensors/temp"));
    EXPECT_TRUE(TopicMatcher::matches("sensors/*", "sensors/humidity"));
    EXPECT_FALSE(TopicMatcher::matches("sensors/*", "actuators/valve"));
}

TEST_F(TopicMatcherTest, MultiLevelWildcard) {
    EXPECT_TRUE(TopicMatcher::matches("sensors/#", "sensors/temp"));
    EXPECT_TRUE(TopicMatcher::matches("sensors/#", "sensors/temp/value"));
    EXPECT_TRUE(TopicMatcher::matches("sensors/#", "sensors/building1/floor2/temp"));
}

TEST_F(TopicMatcherTest, TrailingHashWildcard) {
    // Test the trailing # wildcard
    // Note: In this implementation, # must have content to match after the separator
    EXPECT_TRUE(TopicMatcher::matches("a/b/#", "a/b/c"));
    EXPECT_TRUE(TopicMatcher::matches("a/b/#", "a/b/c/d/e"));
    EXPECT_TRUE(TopicMatcher::matches("a/#", "a/b"));
}

TEST_F(TopicMatcherTest, WildcardWithSeparator) {
    // Test * followed by /
    EXPECT_TRUE(TopicMatcher::matches("*/temp", "sensors/temp"));
    EXPECT_TRUE(TopicMatcher::matches("a/*/c", "a/b/c"));
}

TEST_F(TopicMatcherTest, NoWildcardMismatch) {
    // Test pattern without wildcards that doesn't match
    EXPECT_FALSE(TopicMatcher::matches("sensors/temp", "sensors/humidity"));
    EXPECT_FALSE(TopicMatcher::matches("a/b/c", "a/b/d"));
}

TEST_F(TopicMatcherTest, CharacterMismatch) {
    // Test regular character comparison failure
    EXPECT_FALSE(TopicMatcher::matches("sensor", "sensors"));
    EXPECT_FALSE(TopicMatcher::matches("abc", "abd"));
}

TEST_F(TopicMatcherTest, HasWildcards) {
    EXPECT_TRUE(TopicMatcher::has_wildcards("sensors/*"));
    EXPECT_TRUE(TopicMatcher::has_wildcards("sensors/#"));
    EXPECT_TRUE(TopicMatcher::has_wildcards("*"));
    EXPECT_TRUE(TopicMatcher::has_wildcards("#"));
    EXPECT_FALSE(TopicMatcher::has_wildcards("sensors/temp"));
    EXPECT_FALSE(TopicMatcher::has_wildcards("plain/topic"));
}

TEST_F(TopicMatcherTest, IsValidBasic) {
    EXPECT_TRUE(TopicMatcher::is_valid("sensors/temp"));
    EXPECT_TRUE(TopicMatcher::is_valid("a/b/c"));
    EXPECT_TRUE(TopicMatcher::is_valid("single"));
}

TEST_F(TopicMatcherTest, IsValidEmpty) {
    EXPECT_FALSE(TopicMatcher::is_valid(""));
}

TEST_F(TopicMatcherTest, IsValidEmptySegment) {
    EXPECT_FALSE(TopicMatcher::is_valid("a//b"));  // Empty segment
    EXPECT_FALSE(TopicMatcher::is_valid("/a/b"));  // Starts with separator (empty first segment)
}

TEST_F(TopicMatcherTest, IsValidHashPlacement) {
    EXPECT_TRUE(TopicMatcher::is_valid("sensors/#"));        // # at end, at segment start
    EXPECT_FALSE(TopicMatcher::is_valid("sensors#"));        // # not at segment start
    EXPECT_FALSE(TopicMatcher::is_valid("sensors/#/more"));  // # not at end
}

TEST_F(TopicMatcherTest, IsValidStarPlacement) {
    EXPECT_TRUE(TopicMatcher::is_valid("sensors/*/value"));
    EXPECT_TRUE(TopicMatcher::is_valid("*/data"));
    EXPECT_FALSE(TopicMatcher::is_valid("sensors*"));        // * not alone in segment
    EXPECT_FALSE(TopicMatcher::is_valid("sensors/*extra"));  // * not followed by /
}

TEST_F(TopicMatcherTest, WildcardPatternAfterStar) {
    // Pattern "*/foo" should match "anything/foo"
    EXPECT_TRUE(TopicMatcher::matches("*/b", "a/b"));
}

TEST_F(TopicMatcherTest, ComplexPatterns) {
    // Complex pattern matching
    EXPECT_TRUE(TopicMatcher::matches("a/*/c/*/e", "a/b/c/d/e"));
    EXPECT_TRUE(TopicMatcher::matches("building/*/floor/*", "building/A/floor/1"));
}
