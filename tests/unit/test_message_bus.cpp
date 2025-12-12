/**
 * @file test_message_bus.cpp
 * @brief Comprehensive unit tests for ipb::core::MessageBus and Channel
 */

#include <gtest/gtest.h>
#include <ipb/core/message_bus/message_bus.hpp>
#include <ipb/core/message_bus/channel.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <latch>

using namespace ipb::core;
using namespace ipb::common;
using namespace std::chrono_literals;

// ============================================================================
// Message Tests
// ============================================================================

class MessageTest : public ::testing::Test {};

TEST_F(MessageTest, DefaultConstruction) {
    Message msg;
    EXPECT_EQ(msg.type, Message::Type::DATA_POINT);
    EXPECT_EQ(msg.priority, Message::Priority::NORMAL);
    EXPECT_TRUE(msg.source_id.empty());
    EXPECT_TRUE(msg.topic.empty());
    EXPECT_EQ(msg.sequence, 0);
}

TEST_F(MessageTest, ConstructWithDataPoint) {
    DataPoint dp("sensor1", Value{42.5});
    Message msg(dp);

    EXPECT_EQ(msg.type, Message::Type::DATA_POINT);
    EXPECT_EQ(msg.payload.tag(), "sensor1");
    EXPECT_DOUBLE_EQ(msg.payload.value().get<double>(), 42.5);
}

TEST_F(MessageTest, ConstructWithTopicAndDataPoint) {
    DataPoint dp("temp", Value{25.0});
    Message msg("sensors/temp1", dp);

    EXPECT_EQ(msg.topic, "sensors/temp1");
    EXPECT_EQ(msg.type, Message::Type::DATA_POINT);
}

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

TEST_F(MessageTest, TimestampIsSet) {
    auto before = Timestamp::now();
    Message msg;
    auto after = Timestamp::now();

    EXPECT_GE(msg.timestamp.nanoseconds(), before.nanoseconds());
    EXPECT_LE(msg.timestamp.nanoseconds(), after.nanoseconds());
}

TEST_F(MessageTest, BatchPayload) {
    Message msg;
    msg.type = Message::Type::DATA_BATCH;

    msg.batch_payload.push_back(DataPoint("tag1", Value{1.0}));
    msg.batch_payload.push_back(DataPoint("tag2", Value{2.0}));
    msg.batch_payload.push_back(DataPoint("tag3", Value{3.0}));

    EXPECT_EQ(msg.batch_payload.size(), 3);
}

// ============================================================================
// MessageBusStats Tests
// ============================================================================

class MessageBusStatsTest : public ::testing::Test {};

TEST_F(MessageBusStatsTest, DefaultValues) {
    MessageBusStats stats;

    EXPECT_EQ(stats.messages_published.load(), 0);
    EXPECT_EQ(stats.messages_delivered.load(), 0);
    EXPECT_EQ(stats.messages_dropped.load(), 0);
    EXPECT_EQ(stats.queue_overflows.load(), 0);
    EXPECT_EQ(stats.active_subscriptions.load(), 0);
    EXPECT_EQ(stats.active_channels.load(), 0);
}

TEST_F(MessageBusStatsTest, MessagesPerSecond) {
    MessageBusStats stats;
    stats.messages_published.store(10000);

    double mps = stats.messages_per_second(1s);
    EXPECT_DOUBLE_EQ(mps, 10000.0);
}

TEST_F(MessageBusStatsTest, MessagesPerSecondZeroTime) {
    MessageBusStats stats;
    stats.messages_published.store(10000);

    double mps = stats.messages_per_second(0ms);
    EXPECT_DOUBLE_EQ(mps, 0.0);
}

TEST_F(MessageBusStatsTest, AverageLatency) {
    MessageBusStats stats;
    stats.messages_delivered.store(100);
    stats.total_latency_ns.store(1000000);  // 1ms total

    double avg_us = stats.avg_latency_us();
    EXPECT_DOUBLE_EQ(avg_us, 10.0);  // 10 us average
}

TEST_F(MessageBusStatsTest, Reset) {
    MessageBusStats stats;
    stats.messages_published.store(100);
    stats.messages_delivered.store(95);
    stats.messages_dropped.store(5);

    stats.reset();

    EXPECT_EQ(stats.messages_published.load(), 0);
    EXPECT_EQ(stats.messages_delivered.load(), 0);
    EXPECT_EQ(stats.messages_dropped.load(), 0);
}

// ============================================================================
// MessageBusConfig Tests
// ============================================================================

class MessageBusConfigTest : public ::testing::Test {};

TEST_F(MessageBusConfigTest, DefaultValues) {
    MessageBusConfig config;

    EXPECT_EQ(config.max_channels, 256);
    EXPECT_EQ(config.default_buffer_size, 65536);
    EXPECT_EQ(config.dispatcher_threads, 0);
    EXPECT_TRUE(config.lock_free_mode);
    EXPECT_TRUE(config.priority_dispatch);
    EXPECT_EQ(config.drop_policy, MessageBusConfig::DropPolicy::DROP_OLDEST);
    EXPECT_EQ(config.cpu_affinity, -1);
    EXPECT_EQ(config.realtime_priority, 0);
}

TEST_F(MessageBusConfigTest, DropPolicyValues) {
    EXPECT_NE(
        static_cast<int>(MessageBusConfig::DropPolicy::DROP_NEWEST),
        static_cast<int>(MessageBusConfig::DropPolicy::DROP_OLDEST)
    );
    EXPECT_NE(
        static_cast<int>(MessageBusConfig::DropPolicy::DROP_OLDEST),
        static_cast<int>(MessageBusConfig::DropPolicy::BLOCK)
    );
}

// ============================================================================
// TopicMatcher Tests
// ============================================================================

class TopicMatcherTest : public ::testing::Test {};

TEST_F(TopicMatcherTest, ExactMatch) {
    EXPECT_TRUE(TopicMatcher::matches("sensors/temp1", "sensors/temp1"));
    EXPECT_FALSE(TopicMatcher::matches("sensors/temp1", "sensors/temp2"));
}

TEST_F(TopicMatcherTest, SingleLevelWildcard) {
    EXPECT_TRUE(TopicMatcher::matches("sensors/*", "sensors/temp1"));
    EXPECT_TRUE(TopicMatcher::matches("sensors/*", "sensors/humidity"));
    EXPECT_FALSE(TopicMatcher::matches("sensors/*", "sensors/room1/temp1"));
    EXPECT_FALSE(TopicMatcher::matches("sensors/*", "actuators/valve1"));
}

TEST_F(TopicMatcherTest, MultiLevelWildcard) {
    EXPECT_TRUE(TopicMatcher::matches("sensors/#", "sensors/temp1"));
    EXPECT_TRUE(TopicMatcher::matches("sensors/#", "sensors/room1/temp1"));
    EXPECT_TRUE(TopicMatcher::matches("sensors/#", "sensors/room1/room2/temp1"));
    EXPECT_FALSE(TopicMatcher::matches("sensors/#", "actuators/valve1"));
}

TEST_F(TopicMatcherTest, MixedWildcards) {
    EXPECT_TRUE(TopicMatcher::matches("sensors/*/temp", "sensors/room1/temp"));
    EXPECT_FALSE(TopicMatcher::matches("sensors/*/temp", "sensors/room1/humidity"));
}

TEST_F(TopicMatcherTest, HasWildcards) {
    EXPECT_TRUE(TopicMatcher::has_wildcards("sensors/*"));
    EXPECT_TRUE(TopicMatcher::has_wildcards("sensors/#"));
    EXPECT_TRUE(TopicMatcher::has_wildcards("*/temp"));
    EXPECT_FALSE(TopicMatcher::has_wildcards("sensors/temp1"));
    EXPECT_FALSE(TopicMatcher::has_wildcards("sensors/temp/value"));
}

TEST_F(TopicMatcherTest, IsValid) {
    EXPECT_TRUE(TopicMatcher::is_valid("sensors/temp1"));
    EXPECT_TRUE(TopicMatcher::is_valid("sensors/*"));
    EXPECT_TRUE(TopicMatcher::is_valid("sensors/#"));
    EXPECT_TRUE(TopicMatcher::is_valid("a/b/c/d"));
}

// ============================================================================
// MPMCRingBuffer Tests
// ============================================================================

class MPMCRingBufferTest : public ::testing::Test {};

TEST_F(MPMCRingBufferTest, DefaultConstruction) {
    MPMCRingBuffer<64> buffer;

    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), 64);
}

TEST_F(MPMCRingBufferTest, PushPop) {
    MPMCRingBuffer<64> buffer;

    Message msg(DataPoint("test", Value{42.0}));
    EXPECT_TRUE(buffer.try_push(std::move(msg)));
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 1);

    Message popped;
    EXPECT_TRUE(buffer.try_pop(popped));
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(popped.payload.tag(), "test");
}

TEST_F(MPMCRingBufferTest, FullBuffer) {
    MPMCRingBuffer<4> buffer;  // Small buffer for testing

    // Fill the buffer
    for (int i = 0; i < 3; ++i) {  // capacity - 1
        Message msg(DataPoint("tag", Value{static_cast<double>(i)}));
        EXPECT_TRUE(buffer.try_push(std::move(msg)));
    }

    EXPECT_TRUE(buffer.full());

    // Should fail when full
    Message overflow(DataPoint("overflow", Value{0.0}));
    EXPECT_FALSE(buffer.try_push(std::move(overflow)));
}

TEST_F(MPMCRingBufferTest, EmptyPop) {
    MPMCRingBuffer<64> buffer;

    Message msg;
    EXPECT_FALSE(buffer.try_pop(msg));
}

TEST_F(MPMCRingBufferTest, WrapAround) {
    MPMCRingBuffer<8> buffer;

    // Push and pop multiple times to wrap around
    for (int round = 0; round < 20; ++round) {
        for (int i = 0; i < 5; ++i) {
            Message msg(DataPoint("tag", Value{static_cast<double>(round * 10 + i)}));
            EXPECT_TRUE(buffer.try_push(std::move(msg)));
        }

        for (int i = 0; i < 5; ++i) {
            Message popped;
            EXPECT_TRUE(buffer.try_pop(popped));
            EXPECT_DOUBLE_EQ(popped.payload.value().get<double>(), round * 10 + i);
        }

        EXPECT_TRUE(buffer.empty());
    }
}

TEST_F(MPMCRingBufferTest, ConcurrentProducerConsumer) {
    MPMCRingBuffer<1024> buffer;
    const int num_items = 10000;
    const int num_producers = 4;
    const int num_consumers = 4;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Start producers
    for (int p = 0; p < num_producers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < num_items / num_producers; ++i) {
                Message msg(DataPoint("tag", Value{static_cast<double>(p * 1000 + i)}));
                while (!buffer.try_push(std::move(msg))) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Start consumers
    for (int c = 0; c < num_consumers; ++c) {
        consumers.emplace_back([&]() {
            while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
                Message msg;
                if (buffer.try_pop(msg)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Wait for producers
    for (auto& t : producers) {
        t.join();
    }

    done.store(true, std::memory_order_release);

    // Wait for consumers
    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_EQ(produced.load(), num_items);
    EXPECT_EQ(consumed.load(), num_items);
}

// ============================================================================
// Channel Tests
// ============================================================================

class ChannelTest : public ::testing::Test {};

TEST_F(ChannelTest, Construction) {
    Channel channel("sensors/temp1");

    EXPECT_EQ(channel.topic(), "sensors/temp1");
    EXPECT_EQ(channel.subscriber_count(), 0);
    EXPECT_TRUE(channel.empty());
}

TEST_F(ChannelTest, PublishWithoutSubscriber) {
    Channel channel("test");

    Message msg(DataPoint("tag", Value{42.0}));
    bool result = channel.publish(std::move(msg));

    EXPECT_TRUE(result);
    EXPECT_EQ(channel.messages_received.load(), 1);
}

TEST_F(ChannelTest, SubscribeAndReceive) {
    Channel channel("test");

    std::atomic<int> received_count{0};
    double received_value = 0.0;

    auto sub_id = channel.subscribe([&](const Message& msg) {
        received_count++;
        received_value = msg.payload.value().get<double>();
    });

    EXPECT_GT(sub_id, 0);
    EXPECT_EQ(channel.subscriber_count(), 1);

    // Publish and dispatch
    channel.publish(Message(DataPoint("tag", Value{42.0})));
    channel.dispatch();

    EXPECT_EQ(received_count.load(), 1);
    EXPECT_DOUBLE_EQ(received_value, 42.0);
}

TEST_F(ChannelTest, MultipleSubscribers) {
    Channel channel("test");

    std::atomic<int> count1{0};
    std::atomic<int> count2{0};
    std::atomic<int> count3{0};

    channel.subscribe([&](const Message&) { count1++; });
    channel.subscribe([&](const Message&) { count2++; });
    channel.subscribe([&](const Message&) { count3++; });

    EXPECT_EQ(channel.subscriber_count(), 3);

    channel.publish(Message(DataPoint("tag", Value{1.0})));
    channel.dispatch();

    EXPECT_EQ(count1.load(), 1);
    EXPECT_EQ(count2.load(), 1);
    EXPECT_EQ(count3.load(), 1);
}

TEST_F(ChannelTest, SubscribeWithFilter) {
    Channel channel("test");

    std::atomic<int> received_count{0};

    // Only accept values > 50
    channel.subscribe(
        [&](const Message&) { received_count++; },
        [](const Message& msg) {
            return msg.payload.value().get<double>() > 50.0;
        }
    );

    channel.publish(Message(DataPoint("tag", Value{25.0})));
    channel.publish(Message(DataPoint("tag", Value{75.0})));
    channel.dispatch();

    EXPECT_EQ(received_count.load(), 1);  // Only one passed filter
}

TEST_F(ChannelTest, Unsubscribe) {
    Channel channel("test");

    std::atomic<int> count{0};
    auto sub_id = channel.subscribe([&](const Message&) { count++; });

    EXPECT_EQ(channel.subscriber_count(), 1);

    channel.unsubscribe(sub_id);
    EXPECT_EQ(channel.subscriber_count(), 0);

    channel.publish(Message(DataPoint("tag", Value{1.0})));
    channel.dispatch();

    EXPECT_EQ(count.load(), 0);  // Should not receive after unsubscribe
}

TEST_F(ChannelTest, Statistics) {
    Channel channel("test");

    channel.subscribe([](const Message&) {});

    for (int i = 0; i < 10; ++i) {
        channel.publish(Message(DataPoint("tag", Value{static_cast<double>(i)})));
    }

    EXPECT_EQ(channel.messages_received.load(), 10);

    channel.dispatch();

    EXPECT_EQ(channel.messages_dispatched.load(), 10);
}

// ============================================================================
// Subscription Tests
// ============================================================================

class SubscriptionTest : public ::testing::Test {};

TEST_F(SubscriptionTest, DefaultConstruction) {
    Subscription sub;
    EXPECT_EQ(sub.id(), 0);
    EXPECT_FALSE(sub.is_active());
}

TEST_F(SubscriptionTest, MoveConstruction) {
    auto channel = std::make_shared<Channel>("test");
    auto sub_id = channel->subscribe([](const Message&) {});

    Subscription sub1(sub_id, channel);
    EXPECT_TRUE(sub1.is_active());

    Subscription sub2(std::move(sub1));
    EXPECT_TRUE(sub2.is_active());
}

// ============================================================================
// MessageBus Integration Tests
// ============================================================================

class MessageBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        bus_ = std::make_unique<MessageBus>();
    }

    void TearDown() override {
        if (bus_->is_running()) {
            bus_->stop();
        }
    }

    std::unique_ptr<MessageBus> bus_;
};

TEST_F(MessageBusTest, DefaultConstruction) {
    EXPECT_FALSE(bus_->is_running());
}

TEST_F(MessageBusTest, StartStop) {
    EXPECT_TRUE(bus_->start());
    EXPECT_TRUE(bus_->is_running());

    bus_->stop();
    EXPECT_FALSE(bus_->is_running());
}

TEST_F(MessageBusTest, PublishBeforeStart) {
    // Should still work - messages are queued
    bool result = bus_->publish("test/topic", DataPoint("tag", Value{42.0}));
    EXPECT_TRUE(result);
}

TEST_F(MessageBusTest, GetOrCreateChannel) {
    auto channel1 = bus_->get_or_create_channel("test/topic");
    EXPECT_NE(channel1, nullptr);
    EXPECT_EQ(channel1->topic(), "test/topic");

    // Getting same topic should return same channel
    auto channel2 = bus_->get_or_create_channel("test/topic");
    EXPECT_EQ(channel1, channel2);
}

TEST_F(MessageBusTest, HasChannel) {
    EXPECT_FALSE(bus_->has_channel("test/topic"));

    bus_->get_or_create_channel("test/topic");

    EXPECT_TRUE(bus_->has_channel("test/topic"));
}

TEST_F(MessageBusTest, GetTopics) {
    bus_->get_or_create_channel("topic1");
    bus_->get_or_create_channel("topic2");
    bus_->get_or_create_channel("topic3");

    auto topics = bus_->get_topics();
    EXPECT_EQ(topics.size(), 3);
}

TEST_F(MessageBusTest, ConfigValues) {
    MessageBusConfig config;
    config.max_channels = 128;
    config.default_buffer_size = 32768;
    config.dispatcher_threads = 2;

    MessageBus custom_bus(config);
    const auto& cfg = custom_bus.config();

    EXPECT_EQ(cfg.max_channels, 128);
    EXPECT_EQ(cfg.default_buffer_size, 32768);
    EXPECT_EQ(cfg.dispatcher_threads, 2);
}

TEST_F(MessageBusTest, Statistics) {
    bus_->start();

    const auto& stats = bus_->stats();
    EXPECT_EQ(stats.messages_published.load(), 0);

    bus_->publish("test", DataPoint("tag", Value{1.0}));

    EXPECT_GE(stats.messages_published.load(), 1);
}

TEST_F(MessageBusTest, ResetStats) {
    bus_->start();
    bus_->publish("test", DataPoint("tag", Value{1.0}));

    bus_->reset_stats();

    const auto& stats = bus_->stats();
    EXPECT_EQ(stats.messages_published.load(), 0);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(MPMCRingBufferTest, PushPopPerformance) {
    MPMCRingBuffer<65536> buffer;
    const size_t iterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        Message msg(DataPoint("tag", Value{static_cast<double>(i)}));
        buffer.try_push(std::move(msg));
        Message popped;
        buffer.try_pop(popped);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    EXPECT_LT(ns_per_op, 1000);  // Less than 1us per push+pop

    std::cout << "MPMCRingBuffer push+pop: " << ns_per_op << " ns/op" << std::endl;
}

TEST_F(ChannelTest, DispatchPerformance) {
    Channel channel("test");

    std::atomic<int> count{0};
    channel.subscribe([&](const Message&) { count++; });

    const size_t num_messages = 10000;

    // Pre-fill channel
    for (size_t i = 0; i < num_messages; ++i) {
        channel.publish(Message(DataPoint("tag", Value{static_cast<double>(i)})));
    }

    auto start = std::chrono::high_resolution_clock::now();
    channel.dispatch();
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_msg = duration.count() / num_messages;

    EXPECT_EQ(count.load(), static_cast<int>(num_messages));
    EXPECT_LT(ns_per_msg, 1000);  // Less than 1us per message dispatch

    std::cout << "Channel dispatch: " << ns_per_msg << " ns/msg" << std::endl;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
