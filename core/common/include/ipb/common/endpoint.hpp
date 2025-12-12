#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <optional>
#include <variant>
#include <unordered_map>
#include <memory>
#include <thread>

namespace ipb::common {

/**
 * @brief Network endpoint representation
 */
class EndPoint {
public:
    enum class Protocol : uint8_t {
        TCP = 0,
        UDP,
        UNIX_SOCKET,
        NAMED_PIPE,
        SERIAL,
        USB,
        BLUETOOTH,
        WEBSOCKET,
        HTTP,
        HTTPS,
        MQTT,
        COAP,
        CUSTOM
    };
    
    enum class SecurityLevel : uint8_t {
        NONE = 0,
        BASIC_AUTH,
        TLS,
        MUTUAL_TLS,
        CERTIFICATE,
        TOKEN_BASED,
        CUSTOM
    };
    
    // Default constructor
    EndPoint() = default;
    
    // Constructor for network endpoints
    EndPoint(Protocol protocol, std::string_view host, uint16_t port)
        : protocol_(protocol), host_(host), port_(port) {}
    
    // Constructor for file-based endpoints
    EndPoint(Protocol protocol, std::string_view path)
        : protocol_(protocol), path_(path) {}
    
    // Constructor with full parameters
    EndPoint(Protocol protocol, std::string_view host, uint16_t port,
             std::string_view path, SecurityLevel security = SecurityLevel::NONE)
        : protocol_(protocol), host_(host), port_(port), path_(path), security_level_(security) {}
    
    // Getters
    Protocol protocol() const noexcept { return protocol_; }
    const std::string& host() const noexcept { return host_; }
    uint16_t port() const noexcept { return port_; }
    const std::string& path() const noexcept { return path_; }
    SecurityLevel security_level() const noexcept { return security_level_; }
    
    // Setters
    void set_protocol(Protocol protocol) noexcept { protocol_ = protocol; }
    void set_host(std::string_view host) { host_ = host; }
    void set_port(uint16_t port) noexcept { port_ = port; }
    void set_path(std::string_view path) { path_ = path; }
    void set_security_level(SecurityLevel level) noexcept { security_level_ = level; }
    
    // Connection parameters
    void set_connection_timeout(std::chrono::milliseconds timeout) noexcept {
        connection_timeout_ = timeout;
    }
    
    std::chrono::milliseconds connection_timeout() const noexcept {
        return connection_timeout_;
    }
    
    void set_read_timeout(std::chrono::milliseconds timeout) noexcept {
        read_timeout_ = timeout;
    }
    
    std::chrono::milliseconds read_timeout() const noexcept {
        return read_timeout_;
    }
    
    void set_write_timeout(std::chrono::milliseconds timeout) noexcept {
        write_timeout_ = timeout;
    }
    
    std::chrono::milliseconds write_timeout() const noexcept {
        return write_timeout_;
    }
    
    // Authentication parameters
    void set_username(std::string_view username) { username_ = username; }
    const std::string& username() const noexcept { return username_; }
    
    void set_password(std::string_view password) { password_ = password; }
    const std::string& password() const noexcept { return password_; }
    
    void set_certificate_path(std::string_view cert_path) { certificate_path_ = cert_path; }
    const std::string& certificate_path() const noexcept { return certificate_path_; }
    
    void set_private_key_path(std::string_view key_path) { private_key_path_ = key_path; }
    const std::string& private_key_path() const noexcept { return private_key_path_; }
    
    void set_ca_certificate_path(std::string_view ca_path) { ca_certificate_path_ = ca_path; }
    const std::string& ca_certificate_path() const noexcept { return ca_certificate_path_; }
    
    // Custom properties
    void set_property(std::string_view key, std::string_view value) {
        properties_[std::string(key)] = std::string(value);
    }
    
    std::optional<std::string> get_property(std::string_view key) const {
        auto it = properties_.find(std::string(key));
        return it != properties_.end() ? std::make_optional(it->second) : std::nullopt;
    }
    
    // URL generation
    std::string to_url() const {
        std::string url;
        
        switch (protocol_) {
            case Protocol::TCP:
                url = "tcp://";
                break;
            case Protocol::UDP:
                url = "udp://";
                break;
            case Protocol::HTTP:
                url = "http://";
                break;
            case Protocol::HTTPS:
                url = "https://";
                break;
            case Protocol::WEBSOCKET:
                url = "ws://";
                break;
            case Protocol::MQTT:
                url = "mqtt://";
                break;
            case Protocol::UNIX_SOCKET:
                return "unix://" + path_;
            case Protocol::NAMED_PIPE:
                return "pipe://" + path_;
            case Protocol::SERIAL:
                return "serial://" + path_;
            default:
                url = "unknown://";
                break;
        }
        
        if (!username_.empty()) {
            url += username_;
            if (!password_.empty()) {
                url += ":" + password_;
            }
            url += "@";
        }
        
        url += host_;
        
        if (port_ != 0) {
            url += ":" + std::to_string(port_);
        }
        
        if (!path_.empty()) {
            if (path_[0] != '/') {
                url += "/";
            }
            url += path_;
        }
        
        return url;
    }
    
    // Parse from URL
    static EndPoint from_url(std::string_view url);
    
    // Validation
    bool is_valid() const noexcept {
        switch (protocol_) {
            case Protocol::TCP:
            case Protocol::UDP:
            case Protocol::HTTP:
            case Protocol::HTTPS:
            case Protocol::WEBSOCKET:
            case Protocol::MQTT:
                return !host_.empty() && port_ != 0;
            
            case Protocol::UNIX_SOCKET:
            case Protocol::NAMED_PIPE:
            case Protocol::SERIAL:
                return !path_.empty();
            
            default:
                return false;
        }
    }
    
    // Comparison operators
    bool operator==(const EndPoint& other) const noexcept {
        return protocol_ == other.protocol_ &&
               host_ == other.host_ &&
               port_ == other.port_ &&
               path_ == other.path_;
    }
    
    bool operator!=(const EndPoint& other) const noexcept {
        return !(*this == other);
    }
    
    // Hash support
    size_t hash() const noexcept;

private:
    Protocol protocol_ = Protocol::TCP;
    std::string host_;
    uint16_t port_ = 0;
    std::string path_;
    SecurityLevel security_level_ = SecurityLevel::NONE;
    
    // Timeouts
    std::chrono::milliseconds connection_timeout_{5000};
    std::chrono::milliseconds read_timeout_{1000};
    std::chrono::milliseconds write_timeout_{1000};
    
    // Authentication
    std::string username_;
    std::string password_;
    std::string certificate_path_;
    std::string private_key_path_;
    std::string ca_certificate_path_;
    
    // Custom properties
    std::unordered_map<std::string, std::string> properties_;
};

/**
 * @brief Connection state management
 */
enum class ConnectionState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    ERROR,
    RECONNECTING
};

/**
 * @brief Connection statistics
 */
struct ConnectionStats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t messages_sent = 0;
    uint64_t messages_received = 0;
    uint64_t connection_attempts = 0;
    uint64_t successful_connections = 0;
    uint64_t failed_connections = 0;
    uint64_t disconnections = 0;
    
    std::chrono::steady_clock::time_point last_connect_time;
    std::chrono::steady_clock::time_point last_disconnect_time;
    std::chrono::steady_clock::time_point last_activity_time;
    
    std::chrono::nanoseconds total_connected_time{0};
    std::chrono::nanoseconds min_response_time{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_response_time{0};
    std::chrono::nanoseconds avg_response_time{0};
    
    void reset() noexcept {
        *this = ConnectionStats{};
    }
    
    double connection_success_rate() const noexcept {
        return connection_attempts > 0 ?
            static_cast<double>(successful_connections) / connection_attempts * 100.0 : 0.0;
    }
    
    double uptime_percentage(std::chrono::steady_clock::time_point start_time) const noexcept {
        auto total_time = std::chrono::steady_clock::now() - start_time;
        return total_time.count() > 0 ?
            static_cast<double>(total_connected_time.count()) / total_time.count() * 100.0 : 0.0;
    }
};

/**
 * @brief Real-time primitives for high-performance operations
 */
namespace rt {

/**
 * @brief Lock-free ring buffer for single producer, single consumer
 */
template<typename T, size_t Size>
class SPSCRingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
public:
    SPSCRingBuffer() noexcept : head_(0), tail_(0) {}
    
    // Producer interface
    bool try_push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Size - 1);
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }
        
        buffer_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    bool try_push(T&& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Size - 1);
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }
        
        buffer_[current_tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    // Consumer interface
    bool try_pop(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }
        
        item = std::move(buffer_[current_head]);
        head_.store((current_head + 1) & (Size - 1), std::memory_order_release);
        return true;
    }
    
    // Status
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    bool full() const noexcept {
        const size_t current_tail = tail_.load(std::memory_order_acquire);
        const size_t next_tail = (current_tail + 1) & (Size - 1);
        return next_tail == head_.load(std::memory_order_acquire);
    }
    
    size_t size() const noexcept {
        const size_t current_head = head_.load(std::memory_order_acquire);
        const size_t current_tail = tail_.load(std::memory_order_acquire);
        return (current_tail - current_head) & (Size - 1);
    }
    
    static constexpr size_t capacity() noexcept { return Size - 1; }

private:
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    T buffer_[Size];
};

/**
 * @brief Memory pool for zero-allocation operations
 */
template<typename T, size_t PoolSize>
class MemoryPool {
public:
    MemoryPool() noexcept {
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            pool_[i].next = &pool_[i + 1];
        }
        pool_[PoolSize - 1].next = nullptr;
        free_list_.store(&pool_[0], std::memory_order_relaxed);
    }
    
    T* acquire() noexcept {
        Node* node = free_list_.load(std::memory_order_acquire);
        
        while (node != nullptr) {
            Node* next = node->next;
            if (free_list_.compare_exchange_weak(node, next, 
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                return &node->data;
            }
        }
        
        return nullptr; // Pool exhausted
    }
    
    void release(T* ptr) noexcept {
        if (ptr == nullptr) return;
        
        Node* node = reinterpret_cast<Node*>(ptr);
        Node* current_head = free_list_.load(std::memory_order_relaxed);
        
        do {
            node->next = current_head;
        } while (!free_list_.compare_exchange_weak(current_head, node,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed));
    }
    
    size_t available() const noexcept {
        size_t count = 0;
        Node* current = free_list_.load(std::memory_order_acquire);
        
        while (current != nullptr) {
            ++count;
            current = current->next;
        }
        
        return count;
    }
    
    static constexpr size_t capacity() noexcept { return PoolSize; }

private:
    struct Node {
        union {
            T data;
            Node* next;
        };
        
        Node() noexcept : next(nullptr) {}
        ~Node() noexcept {}
    };
    
    alignas(64) std::atomic<Node*> free_list_;
    Node pool_[PoolSize];
};

/**
 * @brief High-resolution timer for real-time operations
 */
class HighResolutionTimer {
public:
    using clock_type = std::chrono::high_resolution_clock;
    using time_point = clock_type::time_point;
    using duration = clock_type::duration;
    
    HighResolutionTimer() noexcept : start_time_(clock_type::now()) {}
    
    void reset() noexcept {
        start_time_ = clock_type::now();
    }
    
    duration elapsed() const noexcept {
        return clock_type::now() - start_time_;
    }
    
    template<typename Rep, typename Period>
    bool has_elapsed(const std::chrono::duration<Rep, Period>& timeout) const noexcept {
        return elapsed() >= timeout;
    }
    
    static time_point now() noexcept {
        return clock_type::now();
    }

private:
    time_point start_time_;
};

/**
 * @brief CPU affinity management for real-time threads
 */
class CPUAffinity {
public:
    static bool set_thread_affinity(std::thread::id thread_id, int cpu_id) noexcept;
    static bool set_current_thread_affinity(int cpu_id) noexcept;
    static int get_cpu_count() noexcept;
    static std::vector<int> get_available_cpus() noexcept;
    static bool isolate_cpu(int cpu_id) noexcept;
};

/**
 * @brief Real-time thread priority management
 */
class ThreadPriority {
public:
    enum class Level : int {
        LOWEST = 0,
        LOW = 25,
        NORMAL = 50,
        HIGH = 75,
        HIGHEST = 99,
        REALTIME = 100
    };
    
    static bool set_thread_priority(std::thread::id thread_id, Level priority) noexcept;
    static bool set_current_thread_priority(Level priority) noexcept;
    static bool set_realtime_priority(std::thread::id thread_id, int priority) noexcept;
    static bool set_current_realtime_priority(int priority) noexcept;
};

} // namespace rt

} // namespace ipb::common

// Hash specialization for EndPoint
namespace std {
    template<>
    struct hash<ipb::common::EndPoint> {
        size_t operator()(const ipb::common::EndPoint& endpoint) const noexcept {
            return endpoint.hash();
        }
    };
}

