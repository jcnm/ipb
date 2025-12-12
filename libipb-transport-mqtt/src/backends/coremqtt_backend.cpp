#include "ipb/transport/mqtt/backends/coremqtt_backend.hpp"
#include "ipb/transport/mqtt/mqtt_connection.hpp"

#ifdef IPB_HAS_COREMQTT

// coreMQTT headers
extern "C" {
#include "core_mqtt.h"
#include "core_mqtt_state.h"
}

// TLS support via libipb-security (optional)
#ifdef IPB_HAS_SECURITY
#include <ipb/security/tls_context.hpp>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <chrono>
#include <iostream>

namespace ipb::transport::mqtt {

//=============================================================================
// Network Transport for coreMQTT
//=============================================================================

struct NetworkContext {
    int socket_fd = -1;
    bool connected = false;
    std::string hostname;
    uint16_t port = 1883;

#ifdef IPB_HAS_SECURITY
    // TLS support via libipb-security
    std::unique_ptr<ipb::security::TLSContext> tls_context;
    std::unique_ptr<ipb::security::TLSSocket> tls_socket;
    bool use_tls = false;
#endif
};

// Transport send function for coreMQTT
static int32_t transport_send(NetworkContext* ctx, const void* data, size_t len) {
    if (!ctx || ctx->socket_fd < 0) return -1;

#ifdef IPB_HAS_SECURITY
    // Use TLS socket if available
    if (ctx->use_tls && ctx->tls_socket) {
        ssize_t sent = ctx->tls_socket->write(data, len);
        return static_cast<int32_t>(sent);
    }
#endif

    ssize_t sent = ::send(ctx->socket_fd, data, len, MSG_NOSIGNAL);
    return static_cast<int32_t>(sent);
}

// Transport receive function for coreMQTT
static int32_t transport_recv(NetworkContext* ctx, void* data, size_t len) {
    if (!ctx || ctx->socket_fd < 0) return -1;

#ifdef IPB_HAS_SECURITY
    // Use TLS socket if available
    if (ctx->use_tls && ctx->tls_socket) {
        ssize_t received = ctx->tls_socket->read(data, len);
        if (received == 0) return -1;  // Connection closed
        if (received < 0) return 0;    // Would block or error
        return static_cast<int32_t>(received);
    }
#endif

    ssize_t received = ::recv(ctx->socket_fd, data, len, 0);
    if (received == 0) return -1;  // Connection closed
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return static_cast<int32_t>(received);
}

// Get current time in milliseconds
static uint32_t get_time_ms() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    );
    return static_cast<uint32_t>(ms.count());
}

//=============================================================================
// CoreMQTT Backend Implementation
//=============================================================================

class CoreMQTTBackend::Impl {
public:
    explicit Impl(size_t network_buffer_size, size_t max_subscriptions)
        : network_buffer_(network_buffer_size)
        , fixed_buffer_(network_buffer_size)
    {
        (void)max_subscriptions;  // Used for subscription tracking
        std::memset(&mqtt_context_, 0, sizeof(mqtt_context_));
        std::memset(&network_context_, 0, sizeof(network_context_));
    }

    ~Impl() {
        close_socket();
    }

    bool connect_socket(const std::string& hostname, uint16_t port) {
        network_context_.hostname = hostname;
        network_context_.port = port;

        // Resolve hostname
        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        std::string port_str = std::to_string(port);
        if (getaddrinfo(hostname.c_str(), port_str.c_str(), &hints, &result) != 0) {
            return false;
        }

        // Try to connect
        int sockfd = -1;
        for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
            sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sockfd < 0) continue;

            if (::connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
                break;
            }

            ::close(sockfd);
            sockfd = -1;
        }

        freeaddrinfo(result);

        if (sockfd < 0) {
            return false;
        }

        // Set non-blocking
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        network_context_.socket_fd = sockfd;
        network_context_.connected = true;

        return true;
    }

    void close_socket() {
        if (network_context_.socket_fd >= 0) {
            ::close(network_context_.socket_fd);
            network_context_.socket_fd = -1;
        }
        network_context_.connected = false;
    }

    bool initialize_mqtt(const std::string& client_id,
                         uint16_t keep_alive_seconds,
                         CoreMQTTBackend* backend) {
        // Transport interface
        transport_interface_.pNetworkContext = &network_context_;
        transport_interface_.send = reinterpret_cast<TransportSend_t>(transport_send);
        transport_interface_.recv = reinterpret_cast<TransportRecv_t>(transport_recv);

        // Fixed buffer for coreMQTT
        fixed_buffer_struct_.pBuffer = fixed_buffer_.data();
        fixed_buffer_struct_.size = fixed_buffer_.size();

        // Initialize MQTT context
        MQTTStatus_t status = MQTT_Init(
            &mqtt_context_,
            &transport_interface_,
            get_time_ms,
            event_callback,
            &fixed_buffer_struct_
        );

        if (status != MQTTSuccess) {
            return false;
        }

        // Store backend pointer for callbacks
        mqtt_context_.appCallbackContext = backend;

        // Store connection info
        client_id_ = client_id;
        keep_alive_seconds_ = keep_alive_seconds;

        return true;
    }

    MQTTStatus_t mqtt_connect(bool clean_session) {
        MQTTConnectInfo_t connect_info{};
        connect_info.cleanSession = clean_session;
        connect_info.pClientIdentifier = client_id_.c_str();
        connect_info.clientIdentifierLength = static_cast<uint16_t>(client_id_.size());
        connect_info.keepAliveSeconds = keep_alive_seconds_;

        // TODO: Add username/password, will message support

        bool session_present = false;
        return MQTT_Connect(&mqtt_context_, &connect_info, nullptr, 5000, &session_present);
    }

    MQTTStatus_t mqtt_disconnect() {
        return MQTT_Disconnect(&mqtt_context_);
    }

    MQTTStatus_t mqtt_subscribe(const char* topic, size_t topic_len, uint8_t qos) {
        MQTTSubscribeInfo_t sub_info{};
        sub_info.pTopicFilter = topic;
        sub_info.topicFilterLength = static_cast<uint16_t>(topic_len);
        sub_info.qos = static_cast<MQTTQoS_t>(qos);

        return MQTT_Subscribe(&mqtt_context_, &sub_info, 1, MQTT_GetPacketId(&mqtt_context_));
    }

    MQTTStatus_t mqtt_unsubscribe(const char* topic, size_t topic_len) {
        MQTTSubscribeInfo_t unsub_info{};
        unsub_info.pTopicFilter = topic;
        unsub_info.topicFilterLength = static_cast<uint16_t>(topic_len);

        return MQTT_Unsubscribe(&mqtt_context_, &unsub_info, 1, MQTT_GetPacketId(&mqtt_context_));
    }

    MQTTStatus_t mqtt_publish(const char* topic, size_t topic_len,
                              const uint8_t* payload, size_t payload_len,
                              uint8_t qos, bool retained,
                              uint16_t& packet_id_out) {
        MQTTPublishInfo_t pub_info{};
        pub_info.pTopicName = topic;
        pub_info.topicNameLength = static_cast<uint16_t>(topic_len);
        pub_info.pPayload = payload;
        pub_info.payloadLength = payload_len;
        pub_info.qos = static_cast<MQTTQoS_t>(qos);
        pub_info.retain = retained;

        packet_id_out = (qos > 0) ? MQTT_GetPacketId(&mqtt_context_) : 0;

        return MQTT_Publish(&mqtt_context_, &pub_info, packet_id_out);
    }

    MQTTStatus_t mqtt_process_loop(uint32_t timeout_ms) {
        return MQTT_ProcessLoop(&mqtt_context_, timeout_ms);
    }

    MQTTStatus_t mqtt_ping() {
        return MQTT_Ping(&mqtt_context_);
    }

    bool is_socket_connected() const {
        return network_context_.connected && network_context_.socket_fd >= 0;
    }

    size_t buffer_size() const {
        return network_buffer_.size() + fixed_buffer_.size();
    }

    NetworkContext& get_network_context() {
        return network_context_;
    }

private:
    // Event callback for coreMQTT
    static void event_callback(MQTTContext_t* mqtt_ctx,
                               MQTTPacketInfo_t* packet_info,
                               MQTTDeserializedInfo_t* deserialized_info) {
        auto* backend = static_cast<CoreMQTTBackend*>(mqtt_ctx->appCallbackContext);
        if (!backend) return;

        if (packet_info->type == MQTT_PACKET_TYPE_PUBLISH) {
            auto* pub_info = deserialized_info->pPublishInfo;
            if (pub_info) {
                backend->on_incoming_publish(
                    pub_info->pTopicName,
                    pub_info->topicNameLength,
                    static_cast<const uint8_t*>(pub_info->pPayload),
                    pub_info->payloadLength,
                    static_cast<uint8_t>(pub_info->qos),
                    pub_info->retain
                );
            }
        } else if (packet_info->type == MQTT_PACKET_TYPE_PUBACK ||
                   packet_info->type == MQTT_PACKET_TYPE_PUBCOMP) {
            backend->on_ack_received(deserialized_info->packetIdentifier, true);
        }
    }

    MQTTContext_t mqtt_context_;
    NetworkContext network_context_;
    TransportInterface_t transport_interface_{};
    MQTTFixedBuffer_t fixed_buffer_struct_{};

    std::vector<uint8_t> network_buffer_;
    std::vector<uint8_t> fixed_buffer_;

    std::string client_id_;
    uint16_t keep_alive_seconds_ = 60;
};

//=============================================================================
// CoreMQTTBackend Public Methods
//=============================================================================

CoreMQTTBackend::CoreMQTTBackend()
    : CoreMQTTBackend(CoreMQTTBufferConfig::NETWORK_BUFFER_SIZE,
                      CoreMQTTBufferConfig::MAX_SUBSCRIPTIONS) {}

CoreMQTTBackend::CoreMQTTBackend(size_t network_buffer_size, size_t max_subscriptions)
    : impl_(std::make_unique<Impl>(network_buffer_size, max_subscriptions)) {}

CoreMQTTBackend::~CoreMQTTBackend() {
    if (is_connected()) {
        disconnect(1000);
    }
}

bool CoreMQTTBackend::initialize(const ConnectionConfig& config) {
    client_id_ = config.client_id;

    // Parse broker URL for hostname and port
    std::string url = config.broker_url;
    std::string hostname = "localhost";
    uint16_t port = 1883;

    // Simple URL parsing (tcp://hostname:port)
    bool use_tls = false;
    if (url.find("tcp://") == 0) {
        url = url.substr(6);
    } else if (url.find("ssl://") == 0 || url.find("tls://") == 0) {
        url = url.substr(6);
        port = 8883;
        use_tls = true;
    }

    // Check if TLS is required by security mode
    if (config.security != SecurityMode::NONE) {
        use_tls = true;
    }

    auto colon_pos = url.find(':');
    if (colon_pos != std::string::npos) {
        hostname = url.substr(0, colon_pos);
        port = static_cast<uint16_t>(std::stoi(url.substr(colon_pos + 1)));
    } else {
        hostname = url;
    }

    // Connect socket first
    if (!impl_->connect_socket(hostname, port)) {
        state_.store(ConnectionState::FAILED);
        return false;
    }

#ifdef IPB_HAS_SECURITY
    // Setup TLS if required
    if (use_tls) {
        if (!setup_tls(config)) {
            impl_->close_socket();
            state_.store(ConnectionState::FAILED);
            std::cerr << "CoreMQTTBackend: TLS setup failed\n";
            return false;
        }
    }
#else
    if (use_tls) {
        impl_->close_socket();
        state_.store(ConnectionState::FAILED);
        std::cerr << "CoreMQTTBackend: TLS requested but IPB_HAS_SECURITY not enabled\n";
        return false;
    }
#endif

    // Initialize MQTT layer
    if (!impl_->initialize_mqtt(config.client_id, config.keep_alive_seconds, this)) {
        impl_->close_socket();
        state_.store(ConnectionState::FAILED);
        return false;
    }

    state_.store(ConnectionState::DISCONNECTED);
    return true;
}

bool CoreMQTTBackend::connect() {
    if (!impl_->is_socket_connected()) {
        return false;
    }

    state_.store(ConnectionState::CONNECTING);

    auto status = impl_->mqtt_connect(true);
    if (status != MQTTSuccess) {
        state_.store(ConnectionState::FAILED);
        notify_connection_state(ConnectionState::FAILED, "MQTT CONNECT failed");
        return false;
    }

    state_.store(ConnectionState::CONNECTED);
    notify_connection_state(ConnectionState::CONNECTED, "Connected");
    return true;
}

void CoreMQTTBackend::disconnect(uint32_t timeout_ms) {
    (void)timeout_ms;

    if (state_.load() == ConnectionState::CONNECTED) {
        impl_->mqtt_disconnect();
    }

    impl_->close_socket();
    state_.store(ConnectionState::DISCONNECTED);
    notify_connection_state(ConnectionState::DISCONNECTED, "Disconnected");
}

bool CoreMQTTBackend::is_connected() const noexcept {
    return state_.load() == ConnectionState::CONNECTED && impl_->is_socket_connected();
}

ConnectionState CoreMQTTBackend::state() const noexcept {
    return state_.load();
}

std::string_view CoreMQTTBackend::client_id() const noexcept {
    return client_id_;
}

uint16_t CoreMQTTBackend::publish(
    std::string_view topic,
    std::span<const uint8_t> payload,
    QoS qos,
    bool retained)
{
    if (!is_connected()) return 0;

    auto start = std::chrono::high_resolution_clock::now();

    uint16_t packet_id = 0;
    auto status = impl_->mqtt_publish(
        topic.data(), topic.size(),
        payload.data(), payload.size(),
        static_cast<uint8_t>(qos),
        retained,
        packet_id
    );

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    if (status == MQTTSuccess) {
        stats_.messages_sent++;
        stats_.bytes_sent += payload.size();
        stats_.total_publish_time_ns += duration.count();
        stats_.publish_count++;
        return packet_id > 0 ? packet_id : 1;
    }

    stats_.messages_failed++;
    return 0;
}

bool CoreMQTTBackend::publish_sync(
    std::string_view topic,
    std::span<const uint8_t> payload,
    QoS qos,
    bool retained,
    uint32_t timeout_ms)
{
    uint16_t token = publish(topic, payload, qos, retained);
    if (token == 0) return false;

    // For QoS 0, no acknowledgment needed
    if (qos == QoS::AT_MOST_ONCE) return true;

    // Process events until we get ack or timeout
    auto start = std::chrono::steady_clock::now();
    while (true) {
        process_events(10);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        );
        if (elapsed.count() >= timeout_ms) {
            return false;
        }

        // In a real implementation, we'd track the specific packet_id
        // For now, assume success after processing
        break;
    }

    return true;
}

bool CoreMQTTBackend::subscribe(std::string_view topic, QoS qos) {
    if (!is_connected()) return false;

    auto status = impl_->mqtt_subscribe(
        topic.data(), topic.size(),
        static_cast<uint8_t>(qos)
    );

    return status == MQTTSuccess;
}

bool CoreMQTTBackend::unsubscribe(std::string_view topic) {
    if (!is_connected()) return false;

    auto status = impl_->mqtt_unsubscribe(topic.data(), topic.size());
    return status == MQTTSuccess;
}

void CoreMQTTBackend::set_connection_callback(ConnectionCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    connection_cb_ = std::move(cb);
}

void CoreMQTTBackend::set_message_callback(MessageCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_cb_ = std::move(cb);
}

void CoreMQTTBackend::set_delivery_callback(DeliveryCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    delivery_cb_ = std::move(cb);
}

int CoreMQTTBackend::process_events(uint32_t timeout_ms) {
    if (!is_connected()) return 0;

    auto status = impl_->mqtt_process_loop(timeout_ms);

    if (status == MQTTSuccess) {
        return 1;
    } else if (status == MQTTNeedMoreBytes) {
        return 0;
    } else {
        // Connection error
        state_.store(ConnectionState::DISCONNECTED);
        notify_connection_state(ConnectionState::DISCONNECTED, "Process loop error");
        return -1;
    }
}

size_t CoreMQTTBackend::static_memory_usage() const noexcept {
    return sizeof(CoreMQTTBackend) + impl_->buffer_size() + client_id_.capacity();
}

std::chrono::milliseconds CoreMQTTBackend::time_since_last_activity() const noexcept {
    // Would need to track this internally
    return std::chrono::milliseconds(0);
}

bool CoreMQTTBackend::needs_ping() const noexcept {
    // Would check keep-alive timer
    return false;
}

bool CoreMQTTBackend::send_ping() {
    if (!is_connected()) return false;
    return impl_->mqtt_ping() == MQTTSuccess;
}

void CoreMQTTBackend::notify_connection_state(ConnectionState new_state, std::string_view reason) {
    state_.store(new_state);

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (connection_cb_) {
        connection_cb_(new_state, reason);
    }
}

void CoreMQTTBackend::on_incoming_publish(
    const char* topic, size_t topic_len,
    const uint8_t* payload, size_t payload_len,
    uint8_t qos, bool retained)
{
    stats_.messages_received++;
    stats_.bytes_received += payload_len;

    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (message_cb_) {
        message_cb_(
            std::string_view(topic, topic_len),
            std::span<const uint8_t>(payload, payload_len),
            static_cast<QoS>(qos),
            retained
        );
    }
}

void CoreMQTTBackend::on_ack_received(uint16_t packet_id, bool success) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (delivery_cb_) {
        delivery_cb_(packet_id, success);
    }
}

#ifdef IPB_HAS_SECURITY
bool CoreMQTTBackend::setup_tls(const ConnectionConfig& config) {
    // Create TLS configuration from MQTT config
    ipb::security::TLSConfig tls_config = ipb::security::TLSConfig::default_client();

    // Set certificate paths from MQTT TLS config
    if (!config.tls.ca_cert_path.empty()) {
        tls_config.ca_file = config.tls.ca_cert_path;
    }
    if (!config.tls.client_cert_path.empty()) {
        tls_config.cert_file = config.tls.client_cert_path;
    }
    if (!config.tls.client_key_path.empty()) {
        tls_config.key_file = config.tls.client_key_path;
    }

    // Set server name for SNI
    auto url_result = parse_broker_url(config.broker_url);
    if (url_result) {
        tls_config.server_name = std::get<1>(*url_result);
    }

    // Set verification mode
    if (!config.tls.verify_server && !config.tls.verify_certificate) {
        tls_config.verify_mode = ipb::security::VerifyMode::NONE;
    } else {
        tls_config.verify_mode = ipb::security::VerifyMode::REQUIRED;
    }

    // Create TLS context
    auto ctx_result = ipb::security::TLSContext::create(tls_config);
    if (!ctx_result.is_success()) {
        std::cerr << "CoreMQTTBackend: Failed to create TLS context: "
                  << ctx_result.error_message() << "\n";
        return false;
    }

    // Get the network context from impl
    auto& net_ctx = impl_->get_network_context();
    net_ctx.tls_context = std::move(ctx_result.value());

    // Wrap the socket with TLS
    auto socket_result = net_ctx.tls_context->wrap_socket(net_ctx.socket_fd);
    if (!socket_result.is_success()) {
        std::cerr << "CoreMQTTBackend: Failed to wrap socket with TLS: "
                  << socket_result.error_message() << "\n";
        return false;
    }

    net_ctx.tls_socket = std::move(socket_result.value());

    // Perform TLS handshake
    auto handshake_status = net_ctx.tls_socket->do_handshake(
        std::chrono::milliseconds{10000});

    if (handshake_status != ipb::security::HandshakeStatus::SUCCESS) {
        std::cerr << "CoreMQTTBackend: TLS handshake failed\n";
        net_ctx.tls_socket.reset();
        net_ctx.tls_context.reset();
        return false;
    }

    net_ctx.use_tls = true;
    return true;
}
#endif

} // namespace ipb::transport::mqtt

#else // !IPB_HAS_COREMQTT

// Stub implementation when coreMQTT is not available
namespace ipb::transport::mqtt {

class CoreMQTTBackend::Impl {
public:
    Impl(size_t, size_t) {}
};

CoreMQTTBackend::CoreMQTTBackend() : impl_(nullptr) {}
CoreMQTTBackend::CoreMQTTBackend(size_t, size_t) : impl_(nullptr) {}
CoreMQTTBackend::~CoreMQTTBackend() = default;

bool CoreMQTTBackend::initialize(const ConnectionConfig&) {
    std::cerr << "coreMQTT backend not available (compile with -DIPB_HAS_COREMQTT=1)\n";
    return false;
}

bool CoreMQTTBackend::connect() { return false; }
void CoreMQTTBackend::disconnect(uint32_t) {}
bool CoreMQTTBackend::is_connected() const noexcept { return false; }
ConnectionState CoreMQTTBackend::state() const noexcept { return ConnectionState::FAILED; }
std::string_view CoreMQTTBackend::client_id() const noexcept { return ""; }

uint16_t CoreMQTTBackend::publish(std::string_view, std::span<const uint8_t>, QoS, bool) { return 0; }
bool CoreMQTTBackend::publish_sync(std::string_view, std::span<const uint8_t>, QoS, bool, uint32_t) { return false; }
bool CoreMQTTBackend::subscribe(std::string_view, QoS) { return false; }
bool CoreMQTTBackend::unsubscribe(std::string_view) { return false; }

void CoreMQTTBackend::set_connection_callback(ConnectionCallback) {}
void CoreMQTTBackend::set_message_callback(MessageCallback) {}
void CoreMQTTBackend::set_delivery_callback(DeliveryCallback) {}

int CoreMQTTBackend::process_events(uint32_t) { return -1; }
size_t CoreMQTTBackend::static_memory_usage() const noexcept { return 0; }
std::chrono::milliseconds CoreMQTTBackend::time_since_last_activity() const noexcept { return {}; }
bool CoreMQTTBackend::needs_ping() const noexcept { return false; }
bool CoreMQTTBackend::send_ping() { return false; }

void CoreMQTTBackend::notify_connection_state(ConnectionState, std::string_view) {}
void CoreMQTTBackend::on_incoming_publish(const char*, size_t, const uint8_t*, size_t, uint8_t, bool) {}
void CoreMQTTBackend::on_ack_received(uint16_t, bool) {}

} // namespace ipb::transport::mqtt

#endif // IPB_HAS_COREMQTT
