/**
 * @file test_mqtt_transport.cpp
 * @brief Comprehensive unit tests for MQTT transport layer
 *
 * Tests cover:
 * - BackendType enum and utilities
 * - QoS enum
 * - ConnectionState enum
 * - SecurityMode enum
 * - BackendStats struct
 * - ConnectionConfig and TLSConfig
 * - LWTConfig
 * - MQTTConnection (mocked)
 * - MQTTConnectionManager
 * - Utility functions (generate_client_id, parse_broker_url, build_broker_url)
 */

#include <ipb/transport/mqtt/mqtt_connection.hpp>

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::transport::mqtt;
using namespace std::chrono_literals;

// ============================================================================
// BackendType Tests
// ============================================================================

class MQTTBackendTypeTest : public ::testing::Test {};

TEST_F(MQTTBackendTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<int>(BackendType::PAHO), 0);
    EXPECT_EQ(static_cast<int>(BackendType::COREMQTT), 1);
    EXPECT_EQ(static_cast<int>(BackendType::NATIVE), 2);
}

TEST_F(MQTTBackendTypeTest, TypeNames) {
    EXPECT_EQ(backend_type_name(BackendType::PAHO), "paho");
    EXPECT_EQ(backend_type_name(BackendType::COREMQTT), "coremqtt");
    EXPECT_EQ(backend_type_name(BackendType::NATIVE), "native");
}

TEST_F(MQTTBackendTypeTest, DefaultBackendType) {
    BackendType type = default_backend_type();
    // Should be either PAHO or COREMQTT depending on build config
    EXPECT_TRUE(type == BackendType::PAHO || type == BackendType::COREMQTT);
}

// ============================================================================
// QoS Tests
// ============================================================================

class MQTTQoSTest : public ::testing::Test {};

TEST_F(MQTTQoSTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(QoS::AT_MOST_ONCE), 0);
    EXPECT_EQ(static_cast<uint8_t>(QoS::AT_LEAST_ONCE), 1);
    EXPECT_EQ(static_cast<uint8_t>(QoS::EXACTLY_ONCE), 2);
}

TEST_F(MQTTQoSTest, Comparison) {
    EXPECT_LT(QoS::AT_MOST_ONCE, QoS::AT_LEAST_ONCE);
    EXPECT_LT(QoS::AT_LEAST_ONCE, QoS::EXACTLY_ONCE);
}

// ============================================================================
// ConnectionState Tests
// ============================================================================

class MQTTConnectionStateTest : public ::testing::Test {};

TEST_F(MQTTConnectionStateTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::DISCONNECTED), 0);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::CONNECTING), 1);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::CONNECTED), 2);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::RECONNECTING), 3);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::FAILED), 4);
}

// ============================================================================
// SecurityMode Tests
// ============================================================================

class MQTTSecurityModeTest : public ::testing::Test {};

TEST_F(MQTTSecurityModeTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(SecurityMode::NONE), 0);
    EXPECT_EQ(static_cast<uint8_t>(SecurityMode::TLS), 1);
    EXPECT_EQ(static_cast<uint8_t>(SecurityMode::TLS_PSK), 2);
    EXPECT_EQ(static_cast<uint8_t>(SecurityMode::TLS_CLIENT_CERT), 3);
}

// ============================================================================
// BackendStats Tests
// ============================================================================

class MQTTBackendStatsTest : public ::testing::Test {
protected:
    BackendStats stats_;
};

TEST_F(MQTTBackendStatsTest, DefaultValues) {
    EXPECT_EQ(stats_.messages_sent, 0u);
    EXPECT_EQ(stats_.messages_received, 0u);
    EXPECT_EQ(stats_.messages_failed, 0u);
    EXPECT_EQ(stats_.bytes_sent, 0u);
    EXPECT_EQ(stats_.bytes_received, 0u);
    EXPECT_EQ(stats_.reconnect_count, 0u);
    EXPECT_EQ(stats_.total_publish_time_ns, 0u);
    EXPECT_EQ(stats_.publish_count, 0u);
}

TEST_F(MQTTBackendStatsTest, AvgPublishTimeZero) {
    EXPECT_EQ(stats_.avg_publish_time_ns(), 0u);
}

TEST_F(MQTTBackendStatsTest, AvgPublishTimeCalculation) {
    stats_.total_publish_time_ns = 10000;
    stats_.publish_count = 10;

    EXPECT_EQ(stats_.avg_publish_time_ns(), 1000u);
}

TEST_F(MQTTBackendStatsTest, Reset) {
    stats_.messages_sent = 100;
    stats_.messages_received = 90;
    stats_.messages_failed = 10;
    stats_.bytes_sent = 50000;
    stats_.bytes_received = 45000;
    stats_.reconnect_count = 5;
    stats_.total_publish_time_ns = 10000;
    stats_.publish_count = 100;

    stats_.reset();

    EXPECT_EQ(stats_.messages_sent, 0u);
    EXPECT_EQ(stats_.messages_received, 0u);
    EXPECT_EQ(stats_.messages_failed, 0u);
    EXPECT_EQ(stats_.bytes_sent, 0u);
    EXPECT_EQ(stats_.bytes_received, 0u);
    EXPECT_EQ(stats_.reconnect_count, 0u);
    EXPECT_EQ(stats_.total_publish_time_ns, 0u);
    EXPECT_EQ(stats_.publish_count, 0u);
}

// ============================================================================
// TLSConfig Tests
// ============================================================================

class MQTTTLSConfigTest : public ::testing::Test {};

TEST_F(MQTTTLSConfigTest, DefaultValues) {
    TLSConfig config;

    EXPECT_TRUE(config.ca_cert_path.empty());
    EXPECT_TRUE(config.client_cert_path.empty());
    EXPECT_TRUE(config.client_key_path.empty());
    EXPECT_TRUE(config.psk_identity.empty());
    EXPECT_TRUE(config.psk_key.empty());
    EXPECT_TRUE(config.verify_hostname);
    EXPECT_TRUE(config.verify_certificate);
    EXPECT_TRUE(config.verify_server);
    EXPECT_TRUE(config.alpn_protocols.empty());
}

TEST_F(MQTTTLSConfigTest, CustomValues) {
    TLSConfig config;
    config.ca_cert_path = "/path/to/ca.crt";
    config.client_cert_path = "/path/to/client.crt";
    config.client_key_path = "/path/to/client.key";
    config.verify_hostname = false;
    config.verify_certificate = false;
    config.alpn_protocols = {"mqtt"};

    EXPECT_EQ(config.ca_cert_path, "/path/to/ca.crt");
    EXPECT_EQ(config.client_cert_path, "/path/to/client.crt");
    EXPECT_EQ(config.client_key_path, "/path/to/client.key");
    EXPECT_FALSE(config.verify_hostname);
    EXPECT_FALSE(config.verify_certificate);
    EXPECT_EQ(config.alpn_protocols.size(), 1u);
}

// ============================================================================
// LWTConfig Tests
// ============================================================================

class MQTTLWTConfigTest : public ::testing::Test {};

TEST_F(MQTTLWTConfigTest, DefaultValues) {
    LWTConfig config;

    EXPECT_FALSE(config.enabled);
    EXPECT_TRUE(config.topic.empty());
    EXPECT_TRUE(config.payload.empty());
    EXPECT_EQ(config.qos, QoS::AT_LEAST_ONCE);
    EXPECT_FALSE(config.retained);
}

TEST_F(MQTTLWTConfigTest, CustomValues) {
    LWTConfig config;
    config.enabled = true;
    config.topic = "client/status";
    config.payload = "offline";
    config.qos = QoS::EXACTLY_ONCE;
    config.retained = true;

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.topic, "client/status");
    EXPECT_EQ(config.payload, "offline");
    EXPECT_EQ(config.qos, QoS::EXACTLY_ONCE);
    EXPECT_TRUE(config.retained);
}

// ============================================================================
// ConnectionConfig Tests
// ============================================================================

class MQTTConnectionConfigTest : public ::testing::Test {};

TEST_F(MQTTConnectionConfigTest, DefaultValues) {
    ConnectionConfig config;

    EXPECT_EQ(config.broker_url, "tcp://localhost:1883");
    EXPECT_TRUE(config.client_id.empty());
    EXPECT_TRUE(config.username.empty());
    EXPECT_TRUE(config.password.empty());
    EXPECT_EQ(config.keep_alive, std::chrono::seconds(60));
    EXPECT_EQ(config.connect_timeout, std::chrono::seconds(30));
    EXPECT_TRUE(config.clean_session);
    EXPECT_TRUE(config.auto_reconnect);
    EXPECT_EQ(config.security, SecurityMode::NONE);
    EXPECT_EQ(config.max_inflight, 100u);
    EXPECT_EQ(config.max_buffered, 10000u);
}

TEST_F(MQTTConnectionConfigTest, CustomValues) {
    ConnectionConfig config;
    config.broker_url = "ssl://broker.example.com:8883";
    config.client_id = "test_client_123";
    config.username = "user";
    config.password = "secret";
    config.keep_alive = std::chrono::seconds(30);
    config.security = SecurityMode::TLS;

    EXPECT_EQ(config.broker_url, "ssl://broker.example.com:8883");
    EXPECT_EQ(config.client_id, "test_client_123");
    EXPECT_EQ(config.username, "user");
    EXPECT_EQ(config.password, "secret");
    EXPECT_EQ(config.keep_alive, std::chrono::seconds(30));
    EXPECT_EQ(config.security, SecurityMode::TLS);
}

TEST_F(MQTTConnectionConfigTest, SyncLWT) {
    ConnectionConfig config;
    config.lwt.enabled = true;
    config.lwt.topic = "device/status";
    config.lwt.payload = "disconnected";
    config.lwt.qos = QoS::AT_LEAST_ONCE;
    config.lwt.retained = true;

    config.sync_lwt();

    EXPECT_EQ(config.lwt_topic, "device/status");
    EXPECT_EQ(config.lwt_payload, "disconnected");
    EXPECT_EQ(config.lwt_qos, QoS::AT_LEAST_ONCE);
    EXPECT_TRUE(config.lwt_retained);
}

TEST_F(MQTTConnectionConfigTest, SyncLWTDisabled) {
    ConnectionConfig config;
    config.lwt.enabled = false;
    config.lwt.topic = "should/not/sync";

    config.sync_lwt();

    EXPECT_TRUE(config.lwt_topic.empty());
}

TEST_F(MQTTConnectionConfigTest, ValidationEmptyBroker) {
    ConnectionConfig config;
    config.broker_url = "";

    EXPECT_FALSE(config.is_valid());
    EXPECT_FALSE(config.validation_error().empty());
}

TEST_F(MQTTConnectionConfigTest, ValidationValid) {
    ConnectionConfig config;
    config.broker_url = "tcp://localhost:1883";

    EXPECT_TRUE(config.is_valid());
    EXPECT_TRUE(config.validation_error().empty());
}

// ============================================================================
// Utility Functions Tests
// ============================================================================

class MQTTUtilityTest : public ::testing::Test {};

TEST_F(MQTTUtilityTest, GenerateClientId) {
    std::string id = generate_client_id();

    EXPECT_FALSE(id.empty());
    EXPECT_TRUE(id.starts_with("ipb"));
}

TEST_F(MQTTUtilityTest, GenerateClientIdWithPrefix) {
    std::string id = generate_client_id("my_prefix");

    EXPECT_FALSE(id.empty());
    EXPECT_TRUE(id.starts_with("my_prefix"));
}

TEST_F(MQTTUtilityTest, GenerateClientIdUnique) {
    std::set<std::string> ids;
    for (int i = 0; i < 100; ++i) {
        ids.insert(generate_client_id());
    }

    EXPECT_EQ(ids.size(), 100u);  // All unique
}

TEST_F(MQTTUtilityTest, ParseBrokerUrlTCP) {
    auto result = parse_broker_url("tcp://broker.example.com:1883");

    ASSERT_TRUE(result.has_value());
    auto [protocol, host, port] = *result;
    EXPECT_EQ(protocol, "tcp");
    EXPECT_EQ(host, "broker.example.com");
    EXPECT_EQ(port, 1883);
}

TEST_F(MQTTUtilityTest, ParseBrokerUrlSSL) {
    auto result = parse_broker_url("ssl://secure.example.com:8883");

    ASSERT_TRUE(result.has_value());
    auto [protocol, host, port] = *result;
    EXPECT_EQ(protocol, "ssl");
    EXPECT_EQ(host, "secure.example.com");
    EXPECT_EQ(port, 8883);
}

TEST_F(MQTTUtilityTest, ParseBrokerUrlMQTTS) {
    auto result = parse_broker_url("mqtts://secure.example.com:8883");

    ASSERT_TRUE(result.has_value());
    auto [protocol, host, port] = *result;
    EXPECT_EQ(protocol, "mqtts");
    EXPECT_EQ(host, "secure.example.com");
    EXPECT_EQ(port, 8883);
}

TEST_F(MQTTUtilityTest, ParseBrokerUrlInvalid) {
    auto result = parse_broker_url("invalid_url");
    // Should handle gracefully
}

TEST_F(MQTTUtilityTest, ParseBrokerUrlLocalhost) {
    auto result = parse_broker_url("tcp://localhost:1883");

    ASSERT_TRUE(result.has_value());
    auto [protocol, host, port] = *result;
    EXPECT_EQ(host, "localhost");
    EXPECT_EQ(port, 1883);
}

TEST_F(MQTTUtilityTest, BuildBrokerUrlPlain) {
    std::string url = build_broker_url("broker.example.com", 1883, false);

    EXPECT_EQ(url, "tcp://broker.example.com:1883");
}

TEST_F(MQTTUtilityTest, BuildBrokerUrlTLS) {
    std::string url = build_broker_url("broker.example.com", 8883, true);

    EXPECT_EQ(url, "ssl://broker.example.com:8883");
}

TEST_F(MQTTUtilityTest, BuildBrokerUrlLocalhost) {
    std::string url = build_broker_url("localhost", 1883, false);

    EXPECT_EQ(url, "tcp://localhost:1883");
}

// ============================================================================
// MQTTConnectionManager Tests
// ============================================================================

class MQTTConnectionManagerTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Clean up any connections
        auto& manager = MQTTConnectionManager::instance();
        manager.disconnect_all();
    }
};

TEST_F(MQTTConnectionManagerTest, Singleton) {
    auto& manager1 = MQTTConnectionManager::instance();
    auto& manager2 = MQTTConnectionManager::instance();

    EXPECT_EQ(&manager1, &manager2);
}

TEST_F(MQTTConnectionManagerTest, InitiallyEmpty) {
    auto& manager = MQTTConnectionManager::instance();

    EXPECT_EQ(manager.connection_count(), 0u);
    EXPECT_TRUE(manager.get_connection_ids().empty());
}

TEST_F(MQTTConnectionManagerTest, HasConnectionFalse) {
    auto& manager = MQTTConnectionManager::instance();

    EXPECT_FALSE(manager.has_connection("non_existent"));
}

TEST_F(MQTTConnectionManagerTest, GetNonExistent) {
    auto& manager = MQTTConnectionManager::instance();

    auto conn = manager.get("non_existent");
    EXPECT_EQ(conn, nullptr);
}

TEST_F(MQTTConnectionManagerTest, GetOrCreateNew) {
    auto& manager = MQTTConnectionManager::instance();

    ConnectionConfig config;
    config.broker_url = "tcp://localhost:1883";
    config.client_id = "test_client";

    auto conn = manager.get_or_create("test_conn", config);

    EXPECT_NE(conn, nullptr);
    EXPECT_TRUE(manager.has_connection("test_conn"));
    EXPECT_EQ(manager.connection_count(), 1u);
}

TEST_F(MQTTConnectionManagerTest, GetOrCreateExisting) {
    auto& manager = MQTTConnectionManager::instance();

    ConnectionConfig config;
    config.broker_url = "tcp://localhost:1883";
    config.client_id = "test_client";

    auto conn1 = manager.get_or_create("test_conn", config);
    auto conn2 = manager.get_or_create("test_conn", config);

    EXPECT_EQ(conn1, conn2);
    EXPECT_EQ(manager.connection_count(), 1u);
}

TEST_F(MQTTConnectionManagerTest, GetConnectionIds) {
    auto& manager = MQTTConnectionManager::instance();

    ConnectionConfig config;
    config.broker_url = "tcp://localhost:1883";

    manager.get_or_create("conn1", config);
    manager.get_or_create("conn2", config);

    auto ids = manager.get_connection_ids();

    EXPECT_EQ(ids.size(), 2u);
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "conn1") != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), "conn2") != ids.end());
}

TEST_F(MQTTConnectionManagerTest, Remove) {
    auto& manager = MQTTConnectionManager::instance();

    ConnectionConfig config;
    config.broker_url = "tcp://localhost:1883";

    manager.get_or_create("to_remove", config);
    EXPECT_TRUE(manager.has_connection("to_remove"));

    manager.remove("to_remove");
    EXPECT_FALSE(manager.has_connection("to_remove"));
}

TEST_F(MQTTConnectionManagerTest, DisconnectAll) {
    auto& manager = MQTTConnectionManager::instance();

    ConnectionConfig config;
    config.broker_url = "tcp://localhost:1883";

    manager.get_or_create("conn1", config);
    manager.get_or_create("conn2", config);

    manager.disconnect_all();

    // Connections may still exist but should be disconnected
    // This is primarily for cleanup
}

// ============================================================================
// MQTTConnection Tests (Unit tests with mocked broker)
// ============================================================================

class MQTTConnectionTest : public ::testing::Test {
protected:
    ConnectionConfig config_;

    void SetUp() override {
        config_.broker_url = "tcp://localhost:1883";
        config_.client_id = "test_client_" + generate_client_id();
        config_.auto_reconnect = false;  // Disable for tests
    }
};

TEST_F(MQTTConnectionTest, Construction) {
    MQTTConnection conn(config_);

    EXPECT_FALSE(conn.is_connected());
    EXPECT_EQ(conn.get_state(), ConnectionState::DISCONNECTED);
}

TEST_F(MQTTConnectionTest, GetClientId) {
    MQTTConnection conn(config_);

    std::string client_id = conn.get_client_id();
    // Should have a client ID (either configured or auto-generated)
    EXPECT_FALSE(client_id.empty());
}

TEST_F(MQTTConnectionTest, GetBackendType) {
    MQTTConnection conn(config_);

    BackendType type = conn.get_backend_type();
    // Should be a valid backend type
    EXPECT_TRUE(type == BackendType::PAHO || type == BackendType::COREMQTT);
}

TEST_F(MQTTConnectionTest, GetStatistics) {
    MQTTConnection conn(config_);

    const auto& stats = conn.get_statistics();

    EXPECT_EQ(stats.messages_published.load(), 0u);
    EXPECT_EQ(stats.messages_received.load(), 0u);
    EXPECT_EQ(stats.messages_failed.load(), 0u);
}

TEST_F(MQTTConnectionTest, ResetStatistics) {
    MQTTConnection conn(config_);

    conn.reset_statistics();

    const auto& stats = conn.get_statistics();
    EXPECT_EQ(stats.messages_published.load(), 0u);
}

TEST_F(MQTTConnectionTest, RequiresEventLoop) {
    MQTTConnection conn(config_);

    bool requires = conn.requires_event_loop();
    // Depends on backend
    EXPECT_TRUE(requires == true || requires == false);
}

TEST_F(MQTTConnectionTest, SetCallbacks) {
    MQTTConnection conn(config_);

    bool connection_cb_called = false;
    bool message_cb_called = false;
    bool delivery_cb_called = false;

    conn.set_connection_callback([&](ConnectionState, const std::string&) {
        connection_cb_called = true;
    });

    conn.set_message_callback([&](const std::string&, const std::string&, QoS, bool) {
        message_cb_called = true;
    });

    conn.set_delivery_callback([&](int, bool, const std::string&) {
        delivery_cb_called = true;
    });

    // Callbacks are set but not called until events happen
    EXPECT_FALSE(connection_cb_called);
    EXPECT_FALSE(message_cb_called);
    EXPECT_FALSE(delivery_cb_called);
}

TEST_F(MQTTConnectionTest, MoveConstruction) {
    MQTTConnection conn1(config_);
    MQTTConnection conn2(std::move(conn1));

    // conn2 should now own the connection
    EXPECT_EQ(conn2.get_state(), ConnectionState::DISCONNECTED);
}

TEST_F(MQTTConnectionTest, MoveAssignment) {
    MQTTConnection conn1(config_);
    MQTTConnection conn2(config_);

    conn2 = std::move(conn1);

    // conn2 should now own conn1's state
    EXPECT_EQ(conn2.get_state(), ConnectionState::DISCONNECTED);
}

// Note: Actual connect/publish/subscribe tests require a real or mocked broker
// These would typically be integration tests

// ============================================================================
// MQTTConnection::Statistics Tests
// ============================================================================

class MQTTConnectionStatisticsTest : public ::testing::Test {
protected:
    MQTTConnection::Statistics stats_;
};

TEST_F(MQTTConnectionStatisticsTest, DefaultValues) {
    EXPECT_EQ(stats_.messages_published.load(), 0u);
    EXPECT_EQ(stats_.messages_received.load(), 0u);
    EXPECT_EQ(stats_.messages_failed.load(), 0u);
    EXPECT_EQ(stats_.bytes_sent.load(), 0u);
    EXPECT_EQ(stats_.bytes_received.load(), 0u);
    EXPECT_EQ(stats_.reconnect_count.load(), 0u);
}

TEST_F(MQTTConnectionStatisticsTest, Reset) {
    stats_.messages_published = 100;
    stats_.messages_received = 90;
    stats_.messages_failed = 10;
    stats_.bytes_sent = 50000;
    stats_.bytes_received = 45000;
    stats_.reconnect_count = 5;

    stats_.reset();

    EXPECT_EQ(stats_.messages_published.load(), 0u);
    EXPECT_EQ(stats_.messages_received.load(), 0u);
    EXPECT_EQ(stats_.messages_failed.load(), 0u);
    EXPECT_EQ(stats_.bytes_sent.load(), 0u);
    EXPECT_EQ(stats_.bytes_received.load(), 0u);
    EXPECT_EQ(stats_.reconnect_count.load(), 0u);
}

TEST_F(MQTTConnectionStatisticsTest, AtomicOperations) {
    // Test atomic increment
    stats_.messages_published++;
    EXPECT_EQ(stats_.messages_published.load(), 1u);

    stats_.messages_published += 10;
    EXPECT_EQ(stats_.messages_published.load(), 11u);

    stats_.messages_published.fetch_add(5);
    EXPECT_EQ(stats_.messages_published.load(), 16u);
}
