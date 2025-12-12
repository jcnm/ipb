/**
 * @file test_endpoint.cpp
 * @brief Unit tests for IPB EndPoint and real-time primitives
 *
 * Tests coverage for:
 * - EndPoint: Construction, URL parsing, serialization
 * - ConnectionStats: Statistics tracking
 * - rt::SPSCRingBuffer: Lock-free ring buffer
 * - rt::MemoryPool: Memory pool
 * - rt::HighResolutionTimer: Timer
 * - rt::CPUAffinity: CPU affinity (platform-specific)
 * - rt::ThreadPriority: Thread priority (platform-specific)
 */

#include <gtest/gtest.h>
#include <ipb/common/endpoint.hpp>
#include <string>
#include <thread>
#include <chrono>

using namespace ipb::common;

// ============================================================================
// EndPoint Tests
// ============================================================================

class EndPointTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(EndPointTest, DefaultConstruction) {
    EndPoint ep;
    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::TCP);
    EXPECT_TRUE(ep.host().empty());
    EXPECT_EQ(ep.port(), 0);
    EXPECT_TRUE(ep.path().empty());
    EXPECT_EQ(ep.security_level(), EndPoint::SecurityLevel::NONE);
}

TEST_F(EndPointTest, NetworkConstruction) {
    EndPoint ep(EndPoint::Protocol::TCP, "localhost", 8080);

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::TCP);
    EXPECT_EQ(ep.host(), "localhost");
    EXPECT_EQ(ep.port(), 8080);
}

TEST_F(EndPointTest, PathConstruction) {
    EndPoint ep(EndPoint::Protocol::UNIX_SOCKET, "/var/run/app.sock");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::UNIX_SOCKET);
    EXPECT_EQ(ep.path(), "/var/run/app.sock");
}

TEST_F(EndPointTest, FullConstruction) {
    EndPoint ep(EndPoint::Protocol::HTTPS, "example.com", 443,
                "/api/v1", EndPoint::SecurityLevel::TLS);

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::HTTPS);
    EXPECT_EQ(ep.host(), "example.com");
    EXPECT_EQ(ep.port(), 443);
    EXPECT_EQ(ep.path(), "/api/v1");
    EXPECT_EQ(ep.security_level(), EndPoint::SecurityLevel::TLS);
}

TEST_F(EndPointTest, Setters) {
    EndPoint ep;

    ep.set_protocol(EndPoint::Protocol::UDP);
    ep.set_host("192.168.1.100");
    ep.set_port(5000);
    ep.set_path("/data");
    ep.set_security_level(EndPoint::SecurityLevel::BASIC_AUTH);

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::UDP);
    EXPECT_EQ(ep.host(), "192.168.1.100");
    EXPECT_EQ(ep.port(), 5000);
    EXPECT_EQ(ep.path(), "/data");
    EXPECT_EQ(ep.security_level(), EndPoint::SecurityLevel::BASIC_AUTH);
}

TEST_F(EndPointTest, Timeouts) {
    EndPoint ep;

    ep.set_connection_timeout(std::chrono::milliseconds(10000));
    ep.set_read_timeout(std::chrono::milliseconds(2000));
    ep.set_write_timeout(std::chrono::milliseconds(3000));

    EXPECT_EQ(ep.connection_timeout(), std::chrono::milliseconds(10000));
    EXPECT_EQ(ep.read_timeout(), std::chrono::milliseconds(2000));
    EXPECT_EQ(ep.write_timeout(), std::chrono::milliseconds(3000));
}

TEST_F(EndPointTest, Authentication) {
    EndPoint ep;

    ep.set_username("admin");
    ep.set_password("secret");
    ep.set_certificate_path("/etc/ssl/client.crt");
    ep.set_private_key_path("/etc/ssl/client.key");
    ep.set_ca_certificate_path("/etc/ssl/ca.crt");

    EXPECT_EQ(ep.username(), "admin");
    EXPECT_EQ(ep.password(), "secret");
    EXPECT_EQ(ep.certificate_path(), "/etc/ssl/client.crt");
    EXPECT_EQ(ep.private_key_path(), "/etc/ssl/client.key");
    EXPECT_EQ(ep.ca_certificate_path(), "/etc/ssl/ca.crt");
}

TEST_F(EndPointTest, CustomProperties) {
    EndPoint ep;

    ep.set_property("client_id", "device_001");
    ep.set_property("qos", "2");

    auto client_id = ep.get_property("client_id");
    auto qos = ep.get_property("qos");
    auto unknown = ep.get_property("nonexistent");

    EXPECT_TRUE(client_id.has_value());
    EXPECT_EQ(*client_id, "device_001");
    EXPECT_TRUE(qos.has_value());
    EXPECT_EQ(*qos, "2");
    EXPECT_FALSE(unknown.has_value());
}

TEST_F(EndPointTest, Validation) {
    // Valid network endpoint
    EndPoint valid_net(EndPoint::Protocol::TCP, "localhost", 8080);
    EXPECT_TRUE(valid_net.is_valid());

    // Invalid: missing port
    EndPoint invalid_net1(EndPoint::Protocol::TCP, "localhost", 0);
    EXPECT_FALSE(invalid_net1.is_valid());

    // Invalid: missing host
    EndPoint invalid_net2;
    invalid_net2.set_protocol(EndPoint::Protocol::TCP);
    invalid_net2.set_port(8080);
    EXPECT_FALSE(invalid_net2.is_valid());

    // Valid file-based endpoint
    EndPoint valid_file(EndPoint::Protocol::UNIX_SOCKET, "/tmp/socket");
    EXPECT_TRUE(valid_file.is_valid());

    // Invalid file-based: empty path
    EndPoint invalid_file(EndPoint::Protocol::UNIX_SOCKET, "");
    EXPECT_FALSE(invalid_file.is_valid());
}

TEST_F(EndPointTest, Equality) {
    EndPoint ep1(EndPoint::Protocol::TCP, "localhost", 8080);
    EndPoint ep2(EndPoint::Protocol::TCP, "localhost", 8080);
    EndPoint ep3(EndPoint::Protocol::TCP, "localhost", 9090);

    EXPECT_TRUE(ep1 == ep2);
    EXPECT_FALSE(ep1 != ep2);
    EXPECT_FALSE(ep1 == ep3);
    EXPECT_TRUE(ep1 != ep3);
}

TEST_F(EndPointTest, Hash) {
    EndPoint ep1(EndPoint::Protocol::TCP, "localhost", 8080);
    EndPoint ep2(EndPoint::Protocol::TCP, "localhost", 8080);
    EndPoint ep3(EndPoint::Protocol::TCP, "localhost", 9090);

    EXPECT_EQ(ep1.hash(), ep2.hash());
    EXPECT_NE(ep1.hash(), ep3.hash());

    // Test std::hash integration
    std::hash<EndPoint> hasher;
    EXPECT_EQ(hasher(ep1), hasher(ep2));
}

// ============================================================================
// EndPoint URL Parsing Tests
// ============================================================================

class EndPointURLTest : public ::testing::Test {};

TEST_F(EndPointURLTest, ParseTCPUrl) {
    auto ep = EndPoint::from_url("tcp://localhost:8080");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::TCP);
    EXPECT_EQ(ep.host(), "localhost");
    EXPECT_EQ(ep.port(), 8080);
}

TEST_F(EndPointURLTest, ParseUDPUrl) {
    auto ep = EndPoint::from_url("udp://192.168.1.1:5000");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::UDP);
    EXPECT_EQ(ep.host(), "192.168.1.1");
    EXPECT_EQ(ep.port(), 5000);
}

TEST_F(EndPointURLTest, ParseHTTPUrl) {
    auto ep = EndPoint::from_url("http://example.com/api");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::HTTP);
    EXPECT_EQ(ep.host(), "example.com");
    EXPECT_EQ(ep.port(), 80);  // Default port
    EXPECT_EQ(ep.path(), "/api");
}

TEST_F(EndPointURLTest, ParseHTTPSUrl) {
    auto ep = EndPoint::from_url("https://secure.example.com:8443/api/v1");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::HTTPS);
    EXPECT_EQ(ep.host(), "secure.example.com");
    EXPECT_EQ(ep.port(), 8443);
    EXPECT_EQ(ep.path(), "/api/v1");
    EXPECT_EQ(ep.security_level(), EndPoint::SecurityLevel::TLS);
}

TEST_F(EndPointURLTest, ParseWebSocketUrl) {
    auto ep = EndPoint::from_url("ws://ws.example.com/socket");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::WEBSOCKET);
    EXPECT_EQ(ep.host(), "ws.example.com");
    EXPECT_EQ(ep.path(), "/socket");
}

TEST_F(EndPointURLTest, ParseSecureWebSocketUrl) {
    auto ep = EndPoint::from_url("wss://wss.example.com/secure");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::WEBSOCKET);
    EXPECT_EQ(ep.security_level(), EndPoint::SecurityLevel::TLS);
}

TEST_F(EndPointURLTest, ParseMQTTUrl) {
    auto ep = EndPoint::from_url("mqtt://broker.example.com");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::MQTT);
    EXPECT_EQ(ep.host(), "broker.example.com");
    EXPECT_EQ(ep.port(), 1883);  // Default MQTT port
}

TEST_F(EndPointURLTest, ParseMQTTSUrl) {
    auto ep = EndPoint::from_url("mqtts://broker.example.com");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::MQTT);
    EXPECT_EQ(ep.security_level(), EndPoint::SecurityLevel::TLS);
    EXPECT_EQ(ep.port(), 8883);  // Default MQTTS port
}

TEST_F(EndPointURLTest, ParseUnixSocketUrl) {
    auto ep = EndPoint::from_url("unix:///var/run/app.sock");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::UNIX_SOCKET);
    EXPECT_EQ(ep.path(), "/var/run/app.sock");
}

TEST_F(EndPointURLTest, ParseNamedPipeUrl) {
    auto ep = EndPoint::from_url("pipe://./pipe/myapp");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::NAMED_PIPE);
}

TEST_F(EndPointURLTest, ParseSerialUrl) {
    auto ep = EndPoint::from_url("serial:///dev/ttyUSB0");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::SERIAL);
    EXPECT_EQ(ep.path(), "/dev/ttyUSB0");
}

TEST_F(EndPointURLTest, ParseWithCredentials) {
    auto ep = EndPoint::from_url("mqtt://user:pass@broker.example.com:1883");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::MQTT);
    EXPECT_EQ(ep.username(), "user");
    EXPECT_EQ(ep.password(), "pass");
    EXPECT_EQ(ep.host(), "broker.example.com");
    EXPECT_EQ(ep.port(), 1883);
}

TEST_F(EndPointURLTest, ParseWithUsernameOnly) {
    auto ep = EndPoint::from_url("http://admin@example.com/");

    EXPECT_EQ(ep.username(), "admin");
    EXPECT_TRUE(ep.password().empty());
}

TEST_F(EndPointURLTest, ParseIPv6Url) {
    auto ep = EndPoint::from_url("tcp://[::1]:8080");

    EXPECT_EQ(ep.protocol(), EndPoint::Protocol::TCP);
    EXPECT_EQ(ep.host(), "::1");
    EXPECT_EQ(ep.port(), 8080);
}

TEST_F(EndPointURLTest, ParseEmptyUrl) {
    auto ep = EndPoint::from_url("");
    // Should return default endpoint
    EXPECT_TRUE(ep.host().empty());
}

TEST_F(EndPointURLTest, ParseInvalidUrl) {
    auto ep = EndPoint::from_url("invalid_url_no_scheme");
    // Should return default endpoint
    EXPECT_TRUE(ep.host().empty());
}

// ============================================================================
// EndPoint to_url() Tests
// ============================================================================

class EndPointToUrlTest : public ::testing::Test {};

TEST_F(EndPointToUrlTest, TCPToUrl) {
    EndPoint ep(EndPoint::Protocol::TCP, "localhost", 8080);
    EXPECT_EQ(ep.to_url(), "tcp://localhost:8080");
}

TEST_F(EndPointToUrlTest, HTTPWithPath) {
    EndPoint ep(EndPoint::Protocol::HTTP, "example.com", 80, "/api");
    EXPECT_EQ(ep.to_url(), "http://example.com:80/api");
}

TEST_F(EndPointToUrlTest, UnixSocketToUrl) {
    EndPoint ep(EndPoint::Protocol::UNIX_SOCKET, "/var/run/app.sock");
    EXPECT_EQ(ep.to_url(), "unix:///var/run/app.sock");
}

TEST_F(EndPointToUrlTest, WithCredentials) {
    EndPoint ep(EndPoint::Protocol::MQTT, "broker.local", 1883);
    ep.set_username("user");
    ep.set_password("pass");

    std::string url = ep.to_url();
    EXPECT_NE(url.find("user:pass@"), std::string::npos);
}

// ============================================================================
// ConnectionStats Tests
// ============================================================================

class ConnectionStatsTest : public ::testing::Test {};

TEST_F(ConnectionStatsTest, DefaultValues) {
    ConnectionStats stats;

    EXPECT_EQ(stats.bytes_sent, 0u);
    EXPECT_EQ(stats.bytes_received, 0u);
    EXPECT_EQ(stats.messages_sent, 0u);
    EXPECT_EQ(stats.messages_received, 0u);
    EXPECT_EQ(stats.connection_attempts, 0u);
    EXPECT_EQ(stats.successful_connections, 0u);
    EXPECT_EQ(stats.failed_connections, 0u);
    EXPECT_EQ(stats.disconnections, 0u);
}

TEST_F(ConnectionStatsTest, Reset) {
    ConnectionStats stats;
    stats.bytes_sent = 1000;
    stats.messages_sent = 50;
    stats.connection_attempts = 10;

    stats.reset();

    EXPECT_EQ(stats.bytes_sent, 0u);
    EXPECT_EQ(stats.messages_sent, 0u);
    EXPECT_EQ(stats.connection_attempts, 0u);
}

TEST_F(ConnectionStatsTest, ConnectionSuccessRate) {
    ConnectionStats stats;

    // No attempts yet
    EXPECT_DOUBLE_EQ(stats.connection_success_rate(), 0.0);

    // 50% success rate
    stats.connection_attempts = 10;
    stats.successful_connections = 5;
    EXPECT_DOUBLE_EQ(stats.connection_success_rate(), 50.0);

    // 100% success rate
    stats.successful_connections = 10;
    EXPECT_DOUBLE_EQ(stats.connection_success_rate(), 100.0);
}

TEST_F(ConnectionStatsTest, UptimePercentage) {
    ConnectionStats stats;
    auto start_time = std::chrono::steady_clock::now();

    stats.total_connected_time = std::chrono::seconds(50);

    // This depends on elapsed time, so we just verify it returns a value
    double uptime = stats.uptime_percentage(start_time);
    EXPECT_GE(uptime, 0.0);
}

// ============================================================================
// rt::SPSCRingBuffer Tests
// ============================================================================

class SPSCRingBufferTest : public ::testing::Test {};

TEST_F(SPSCRingBufferTest, InitialState) {
    rt::SPSCRingBuffer<int, 8> buffer;

    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.capacity(), 7u);  // Size - 1
}

TEST_F(SPSCRingBufferTest, PushPop) {
    rt::SPSCRingBuffer<int, 8> buffer;

    EXPECT_TRUE(buffer.try_push(42));
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 1u);

    int value;
    EXPECT_TRUE(buffer.try_pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(buffer.empty());
}

TEST_F(SPSCRingBufferTest, MovePush) {
    rt::SPSCRingBuffer<std::string, 4> buffer;

    std::string str = "hello";
    EXPECT_TRUE(buffer.try_push(std::move(str)));

    std::string result;
    EXPECT_TRUE(buffer.try_pop(result));
    EXPECT_EQ(result, "hello");
}

TEST_F(SPSCRingBufferTest, FullBuffer) {
    rt::SPSCRingBuffer<int, 4> buffer;  // Capacity is 3

    EXPECT_TRUE(buffer.try_push(1));
    EXPECT_TRUE(buffer.try_push(2));
    EXPECT_TRUE(buffer.try_push(3));

    EXPECT_TRUE(buffer.full());
    EXPECT_FALSE(buffer.try_push(4));  // Should fail
}

TEST_F(SPSCRingBufferTest, EmptyPop) {
    rt::SPSCRingBuffer<int, 4> buffer;

    int value;
    EXPECT_FALSE(buffer.try_pop(value));  // Should fail
}

TEST_F(SPSCRingBufferTest, WrapAround) {
    rt::SPSCRingBuffer<int, 4> buffer;

    // Fill and empty multiple times to test wrap-around
    for (int round = 0; round < 5; ++round) {
        EXPECT_TRUE(buffer.try_push(round * 3 + 1));
        EXPECT_TRUE(buffer.try_push(round * 3 + 2));
        EXPECT_TRUE(buffer.try_push(round * 3 + 3));

        int v1, v2, v3;
        EXPECT_TRUE(buffer.try_pop(v1));
        EXPECT_TRUE(buffer.try_pop(v2));
        EXPECT_TRUE(buffer.try_pop(v3));

        EXPECT_EQ(v1, round * 3 + 1);
        EXPECT_EQ(v2, round * 3 + 2);
        EXPECT_EQ(v3, round * 3 + 3);
    }
}

TEST_F(SPSCRingBufferTest, ConcurrentAccess) {
    rt::SPSCRingBuffer<int, 1024> buffer;
    constexpr int COUNT = 10000;

    std::atomic<bool> start{false};
    std::atomic<int> sum{0};

    // Producer thread
    std::thread producer([&]() {
        while (!start.load()) {}
        for (int i = 0; i < COUNT; ++i) {
            while (!buffer.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread
    std::thread consumer([&]() {
        while (!start.load()) {}
        for (int i = 0; i < COUNT; ++i) {
            int value;
            while (!buffer.try_pop(value)) {
                std::this_thread::yield();
            }
            sum += value;
        }
    });

    start = true;
    producer.join();
    consumer.join();

    // Verify all values were received
    EXPECT_EQ(sum.load(), (COUNT - 1) * COUNT / 2);
}

// ============================================================================
// rt::MemoryPool Tests
// ============================================================================

class MemoryPoolTest : public ::testing::Test {};

TEST_F(MemoryPoolTest, InitialState) {
    rt::MemoryPool<int, 16> pool;

    EXPECT_EQ(pool.available(), 16u);
    EXPECT_EQ(pool.capacity(), 16u);
}

TEST_F(MemoryPoolTest, AcquireRelease) {
    rt::MemoryPool<int, 4> pool;

    int* ptr = pool.acquire();
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(pool.available(), 3u);

    *ptr = 42;
    EXPECT_EQ(*ptr, 42);

    pool.release(ptr);
    EXPECT_EQ(pool.available(), 4u);
}

TEST_F(MemoryPoolTest, Exhaustion) {
    rt::MemoryPool<int, 2> pool;

    int* p1 = pool.acquire();
    int* p2 = pool.acquire();
    int* p3 = pool.acquire();

    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_EQ(p3, nullptr);  // Pool exhausted

    pool.release(p1);
    p3 = pool.acquire();
    EXPECT_NE(p3, nullptr);

    pool.release(p2);
    pool.release(p3);
}

TEST_F(MemoryPoolTest, ReleaseNull) {
    rt::MemoryPool<int, 4> pool;

    // Should handle null gracefully
    pool.release(nullptr);
    EXPECT_EQ(pool.available(), 4u);
}

TEST_F(MemoryPoolTest, ComplexType) {
    struct Data {
        int value;
        std::string name;
    };

    rt::MemoryPool<Data, 4> pool;

    Data* d = pool.acquire();
    EXPECT_NE(d, nullptr);

    pool.release(d);
}

// ============================================================================
// rt::HighResolutionTimer Tests
// ============================================================================

class HighResolutionTimerTest : public ::testing::Test {};

TEST_F(HighResolutionTimerTest, ElapsedTime) {
    rt::HighResolutionTimer timer;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto elapsed = timer.elapsed();
    EXPECT_GE(elapsed, std::chrono::milliseconds(5));
}

TEST_F(HighResolutionTimerTest, Reset) {
    rt::HighResolutionTimer timer;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.reset();

    auto elapsed = timer.elapsed();
    EXPECT_LT(elapsed, std::chrono::milliseconds(10));
}

TEST_F(HighResolutionTimerTest, HasElapsed) {
    rt::HighResolutionTimer timer;

    EXPECT_FALSE(timer.has_elapsed(std::chrono::seconds(1)));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(timer.has_elapsed(std::chrono::milliseconds(10)));
}

TEST_F(HighResolutionTimerTest, Now) {
    auto t1 = rt::HighResolutionTimer::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto t2 = rt::HighResolutionTimer::now();

    EXPECT_GT(t2, t1);
}

// ============================================================================
// rt::CPUAffinity Tests (Platform-specific)
// ============================================================================

class CPUAffinityTest : public ::testing::Test {};

TEST_F(CPUAffinityTest, GetCPUCount) {
    int count = rt::CPUAffinity::get_cpu_count();
    EXPECT_GT(count, 0);
}

TEST_F(CPUAffinityTest, GetAvailableCPUs) {
    auto cpus = rt::CPUAffinity::get_available_cpus();
    EXPECT_FALSE(cpus.empty());
    EXPECT_EQ(cpus.size(), static_cast<size_t>(rt::CPUAffinity::get_cpu_count()));
}

TEST_F(CPUAffinityTest, SetCurrentThreadAffinity) {
    // This may or may not succeed depending on permissions
    // We just verify it doesn't crash
    auto result = rt::CPUAffinity::set_current_thread_affinity(0);
    (void)result;  // Result depends on platform/permissions
}

// ============================================================================
// rt::ThreadPriority Tests (Platform-specific)
// ============================================================================

class ThreadPriorityTest : public ::testing::Test {};

TEST_F(ThreadPriorityTest, SetCurrentThreadPriority) {
    // This may or may not succeed depending on permissions
    // We just verify it doesn't crash
    auto result = rt::ThreadPriority::set_current_thread_priority(
        rt::ThreadPriority::Level::NORMAL);
    (void)result;  // Result depends on platform/permissions
}

TEST_F(ThreadPriorityTest, SetCurrentRealtimePriority) {
    // This requires root permissions typically
    // We just verify it doesn't crash
    auto result = rt::ThreadPriority::set_current_realtime_priority(50);
    (void)result;  // Result depends on platform/permissions
}

// ============================================================================
// ConnectionState Tests
// ============================================================================

class ConnectionStateTest : public ::testing::Test {};

TEST_F(ConnectionStateTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::DISCONNECTED), 0);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::CONNECTING), 1);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::CONNECTED), 2);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::DISCONNECTING), 3);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::ERROR), 4);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::RECONNECTING), 5);
}
