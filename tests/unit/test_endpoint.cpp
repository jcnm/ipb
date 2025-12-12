/**
 * @file test_endpoint.cpp
 * @brief Comprehensive unit tests for ipb::common::EndPoint and RT primitives
 */

#include <gtest/gtest.h>
#include <ipb/common/endpoint.hpp>
#include <thread>
#include <vector>
#include <atomic>

using namespace ipb::common;
using namespace std::chrono_literals;

// ============================================================================
// EndPoint Tests
// ============================================================================

class EndPointTest : public ::testing::Test {};

TEST_F(EndPointTest, DefaultConstruction) {
    EndPoint ep;
    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::TCP);
    EXPECT_TRUE(ep.host().empty());
    EXPECT_EQ(ep.port(), 0);
    EXPECT_TRUE(ep.path().empty());
}

TEST_F(EndPointTest, ConstructWithHostAndPort) {
    EndPoint ep(EndPoint::Protocol::TCP, "192.168.1.100", 502);

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::TCP);
    EXPECT_EQ(ep.host(), "192.168.1.100");
    EXPECT_EQ(ep.port(), 502);
}

TEST_F(EndPointTest, ConstructWithPath) {
    EndPoint ep(EndPoint::Protocol::UNIX_SOCKET, "/var/run/socket.sock");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::UNIX_SOCKET);
    EXPECT_EQ(ep.path(), "/var/run/socket.sock");
}

TEST_F(EndPointTest, SettersGetters) {
    EndPoint ep;

    ep.set_protocol(EndPoint::Protocol::MQTT);
    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::MQTT);

    ep.set_host("broker.example.com");
    EXPECT_EQ(ep.host(), "broker.example.com");

    ep.set_port(1883);
    EXPECT_EQ(ep.port(), 1883);

    ep.set_path("/topic");
    EXPECT_EQ(ep.path(), "/topic");

    ep.set_security_level(EndPoint::SecurityLevel::TLS);
    EXPECT_EQ(ep.security_level(), EndPoint::SecurityLevel::TLS);
}

TEST_F(EndPointTest, TimeoutSettings) {
    EndPoint ep;

    ep.set_connection_timeout(5000ms);
    EXPECT_EQ(ep.connection_timeout().count(), 5000);

    ep.set_read_timeout(1000ms);
    EXPECT_EQ(ep.read_timeout().count(), 1000);

    ep.set_write_timeout(2000ms);
    EXPECT_EQ(ep.write_timeout().count(), 2000);
}

TEST_F(EndPointTest, Authentication) {
    EndPoint ep;

    ep.set_username("admin");
    EXPECT_EQ(ep.username(), "admin");

    ep.set_password("secret");
    EXPECT_EQ(ep.password(), "secret");

    ep.set_certificate_path("/etc/certs/client.crt");
    EXPECT_EQ(ep.certificate_path(), "/etc/certs/client.crt");

    ep.set_private_key_path("/etc/certs/client.key");
    EXPECT_EQ(ep.private_key_path(), "/etc/certs/client.key");

    ep.set_ca_certificate_path("/etc/certs/ca.crt");
    EXPECT_EQ(ep.ca_certificate_path(), "/etc/certs/ca.crt");
}

TEST_F(EndPointTest, CustomProperties) {
    EndPoint ep;

    ep.set_property("custom_key", "custom_value");
    auto value = ep.get_property("custom_key");

    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "custom_value");

    auto missing = ep.get_property("nonexistent");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(EndPointTest, ToUrlTcp) {
    EndPoint ep(EndPoint::Protocol::TCP, "192.168.1.100", 502);
    std::string url = ep.to_url();

    EXPECT_EQ(url, "tcp://192.168.1.100:502");
}

TEST_F(EndPointTest, ToUrlMqtt) {
    EndPoint ep(EndPoint::Protocol::MQTT, "broker.example.com", 1883);
    std::string url = ep.to_url();

    EXPECT_EQ(url, "mqtt://broker.example.com:1883");
}

TEST_F(EndPointTest, ToUrlWithCredentials) {
    EndPoint ep(EndPoint::Protocol::MQTT, "broker.example.com", 1883);
    ep.set_username("user");
    ep.set_password("pass");

    std::string url = ep.to_url();
    EXPECT_NE(url.find("user:pass@"), std::string::npos);
}

TEST_F(EndPointTest, ToUrlUnixSocket) {
    EndPoint ep(EndPoint::Protocol::UNIX_SOCKET, "/var/run/socket.sock");
    std::string url = ep.to_url();

    EXPECT_EQ(url, "unix:///var/run/socket.sock");
}

TEST_F(EndPointTest, IsValidTcp) {
    EndPoint valid(EndPoint::Protocol::TCP, "localhost", 8080);
    EndPoint invalid_no_host(EndPoint::Protocol::TCP, "", 8080);
    EndPoint invalid_no_port(EndPoint::Protocol::TCP, "localhost", 0);

    EXPECT_TRUE(valid.is_valid());
    EXPECT_FALSE(invalid_no_host.is_valid());
    EXPECT_FALSE(invalid_no_port.is_valid());
}

TEST_F(EndPointTest, IsValidUnixSocket) {
    EndPoint valid(EndPoint::Protocol::UNIX_SOCKET, "/var/run/socket.sock");
    EndPoint invalid(EndPoint::Protocol::UNIX_SOCKET, "");

    EXPECT_TRUE(valid.is_valid());
    EXPECT_FALSE(invalid.is_valid());
}

TEST_F(EndPointTest, Equality) {
    EndPoint ep1(EndPoint::Protocol::TCP, "localhost", 8080);
    EndPoint ep2(EndPoint::Protocol::TCP, "localhost", 8080);
    EndPoint ep3(EndPoint::Protocol::TCP, "localhost", 9090);

    EXPECT_TRUE(ep1 == ep2);
    EXPECT_FALSE(ep1 == ep3);
    EXPECT_TRUE(ep1 != ep3);
}

TEST_F(EndPointTest, HashConsistency) {
    EndPoint ep(EndPoint::Protocol::TCP, "localhost", 8080);

    size_t hash1 = ep.hash();
    size_t hash2 = ep.hash();

    EXPECT_EQ(hash1, hash2);
}

// ============================================================================
// ConnectionState Tests
// ============================================================================

TEST_F(EndPointTest, ConnectionStateValues) {
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::DISCONNECTED), 0);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::CONNECTING), 1);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::CONNECTED), 2);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::DISCONNECTING), 3);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::ERROR), 4);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::RECONNECTING), 5);
}

// ============================================================================
// ConnectionStats Tests
// ============================================================================

class ConnectionStatsTest : public ::testing::Test {};

TEST_F(ConnectionStatsTest, DefaultValues) {
    ConnectionStats stats;

    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.bytes_received, 0);
    EXPECT_EQ(stats.messages_sent, 0);
    EXPECT_EQ(stats.messages_received, 0);
    EXPECT_EQ(stats.connection_attempts, 0);
    EXPECT_EQ(stats.successful_connections, 0);
    EXPECT_EQ(stats.failed_connections, 0);
    EXPECT_EQ(stats.disconnections, 0);
}

TEST_F(ConnectionStatsTest, ConnectionSuccessRate) {
    ConnectionStats stats;
    stats.connection_attempts = 100;
    stats.successful_connections = 95;

    EXPECT_DOUBLE_EQ(stats.connection_success_rate(), 95.0);
}

TEST_F(ConnectionStatsTest, ConnectionSuccessRateZeroAttempts) {
    ConnectionStats stats;
    EXPECT_DOUBLE_EQ(stats.connection_success_rate(), 0.0);
}

TEST_F(ConnectionStatsTest, Reset) {
    ConnectionStats stats;
    stats.bytes_sent = 1000;
    stats.messages_sent = 100;

    stats.reset();

    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.messages_sent, 0);
}

// ============================================================================
// SPSCRingBuffer Tests
// ============================================================================

class SPSCRingBufferTest : public ::testing::Test {};

TEST_F(SPSCRingBufferTest, DefaultConstruction) {
    rt::SPSCRingBuffer<int, 16> buffer;

    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), 15);  // Size - 1
}

TEST_F(SPSCRingBufferTest, PushPop) {
    rt::SPSCRingBuffer<int, 16> buffer;

    EXPECT_TRUE(buffer.try_push(42));
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 1);

    int value;
    EXPECT_TRUE(buffer.try_pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(buffer.empty());
}

TEST_F(SPSCRingBufferTest, PushMove) {
    rt::SPSCRingBuffer<std::string, 16> buffer;

    std::string str = "hello";
    EXPECT_TRUE(buffer.try_push(std::move(str)));

    std::string result;
    EXPECT_TRUE(buffer.try_pop(result));
    EXPECT_EQ(result, "hello");
}

TEST_F(SPSCRingBufferTest, FullBuffer) {
    rt::SPSCRingBuffer<int, 4> buffer;  // Capacity = 3

    EXPECT_TRUE(buffer.try_push(1));
    EXPECT_TRUE(buffer.try_push(2));
    EXPECT_TRUE(buffer.try_push(3));
    EXPECT_TRUE(buffer.full());
    EXPECT_FALSE(buffer.try_push(4));  // Should fail
}

TEST_F(SPSCRingBufferTest, EmptyBuffer) {
    rt::SPSCRingBuffer<int, 16> buffer;

    int value;
    EXPECT_FALSE(buffer.try_pop(value));  // Should fail
}

TEST_F(SPSCRingBufferTest, WrapAround) {
    rt::SPSCRingBuffer<int, 4> buffer;  // Capacity = 3

    // Fill and empty multiple times to test wrap-around
    for (int round = 0; round < 5; ++round) {
        EXPECT_TRUE(buffer.try_push(round * 10 + 1));
        EXPECT_TRUE(buffer.try_push(round * 10 + 2));

        int val1, val2;
        EXPECT_TRUE(buffer.try_pop(val1));
        EXPECT_TRUE(buffer.try_pop(val2));

        EXPECT_EQ(val1, round * 10 + 1);
        EXPECT_EQ(val2, round * 10 + 2);
    }
}

TEST_F(SPSCRingBufferTest, ConcurrentAccess) {
    rt::SPSCRingBuffer<int, 1024> buffer;
    const int num_items = 10000;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> done{false};

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_items; ++i) {
            while (!buffer.try_push(i)) {
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::thread consumer([&]() {
        int expected = 0;
        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            int value;
            if (buffer.try_pop(value)) {
                EXPECT_EQ(value, expected);
                expected++;
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(produced.load(), num_items);
    EXPECT_EQ(consumed.load(), num_items);
}

// ============================================================================
// MemoryPool Tests
// ============================================================================

class MemoryPoolTest : public ::testing::Test {};

TEST_F(MemoryPoolTest, AcquireRelease) {
    rt::MemoryPool<int, 10> pool;

    EXPECT_EQ(pool.capacity(), 10);
    EXPECT_EQ(pool.available(), 10);

    int* ptr = pool.acquire();
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(pool.available(), 9);

    pool.release(ptr);
    EXPECT_EQ(pool.available(), 10);
}

TEST_F(MemoryPoolTest, ExhaustPool) {
    rt::MemoryPool<int, 3> pool;

    int* p1 = pool.acquire();
    int* p2 = pool.acquire();
    int* p3 = pool.acquire();

    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p3, nullptr);
    EXPECT_EQ(pool.available(), 0);

    int* p4 = pool.acquire();
    EXPECT_EQ(p4, nullptr);  // Pool exhausted

    pool.release(p1);
    EXPECT_EQ(pool.available(), 1);

    int* p5 = pool.acquire();
    EXPECT_NE(p5, nullptr);
}

TEST_F(MemoryPoolTest, ReleaseNull) {
    rt::MemoryPool<int, 10> pool;

    // Should not crash
    pool.release(nullptr);
    EXPECT_EQ(pool.available(), 10);
}

TEST_F(MemoryPoolTest, DataIntegrity) {
    struct TestStruct {
        int a;
        double b;
        char c[32];
    };

    rt::MemoryPool<TestStruct, 10> pool;

    TestStruct* ptr = pool.acquire();
    ASSERT_NE(ptr, nullptr);

    ptr->a = 42;
    ptr->b = 3.14;
    std::strcpy(ptr->c, "hello");

    pool.release(ptr);

    TestStruct* ptr2 = pool.acquire();
    // Memory might be reused, but we shouldn't depend on values
    EXPECT_NE(ptr2, nullptr);
    pool.release(ptr2);
}

// ============================================================================
// HighResolutionTimer Tests
// ============================================================================

class HighResolutionTimerTest : public ::testing::Test {};

TEST_F(HighResolutionTimerTest, DefaultConstruction) {
    rt::HighResolutionTimer timer;
    auto elapsed = timer.elapsed();

    // Should be very small right after construction
    EXPECT_LT(elapsed.count(), 1000000);  // Less than 1ms
}

TEST_F(HighResolutionTimerTest, Reset) {
    rt::HighResolutionTimer timer;
    std::this_thread::sleep_for(10ms);

    timer.reset();
    auto elapsed = timer.elapsed();

    EXPECT_LT(elapsed.count(), 1000000);  // Less than 1ms after reset
}

TEST_F(HighResolutionTimerTest, HasElapsed) {
    rt::HighResolutionTimer timer;

    EXPECT_FALSE(timer.has_elapsed(1s));

    std::this_thread::sleep_for(50ms);

    EXPECT_TRUE(timer.has_elapsed(10ms));
}

TEST_F(HighResolutionTimerTest, Now) {
    auto t1 = rt::HighResolutionTimer::now();
    std::this_thread::sleep_for(1ms);
    auto t2 = rt::HighResolutionTimer::now();

    EXPECT_GT(t2, t1);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(SPSCRingBufferTest, PushPopPerformance) {
    rt::SPSCRingBuffer<int, 1024> buffer;
    const size_t iterations = 1000000;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        buffer.try_push(static_cast<int>(i));
        int val;
        buffer.try_pop(val);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    EXPECT_LT(ns_per_op, 100);

    std::cout << "SPSCRingBuffer push+pop: " << ns_per_op << " ns/op" << std::endl;
}

TEST_F(MemoryPoolTest, AcquireReleasePerformance) {
    rt::MemoryPool<int, 100> pool;
    const size_t iterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        int* ptr = pool.acquire();
        pool.release(ptr);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    EXPECT_LT(ns_per_op, 200);

    std::cout << "MemoryPool acquire+release: " << ns_per_op << " ns/op" << std::endl;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
