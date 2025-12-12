# IPB API Reference

This document provides detailed API documentation for IPB core components.

## Table of Contents

- [Core Types](#core-types)
  - [Timestamp](#timestamp)
  - [Value](#value)
  - [DataPoint](#datapoint)
  - [Quality](#quality)
- [Error Handling](#error-handling)
  - [ErrorCode](#errorcode)
  - [Result<T>](#resultt)
- [Network](#network)
  - [EndPoint](#endpoint)
  - [ConnectionStats](#connectionstats)
- [Message Bus](#message-bus)
  - [MessageBus](#messagebus)
  - [Message](#message)
  - [Subscription](#subscription)
- [Real-Time Primitives](#real-time-primitives)
  - [SPSCRingBuffer](#spscringbuffer)
  - [MemoryPool](#memorypool)

---

## Core Types

### Timestamp

High-performance timestamp with nanosecond precision, optimized for real-time systems.

**Header:** `<ipb/common/data_point.hpp>`

**Namespace:** `ipb::common`

```cpp
class Timestamp {
public:
    // Types
    using duration_type = std::chrono::nanoseconds;
    using clock_type = std::chrono::steady_clock;

    // Constructors
    constexpr Timestamp() noexcept;
    explicit Timestamp(duration_type ns) noexcept;

    // Factory methods
    static Timestamp now() noexcept;
    static Timestamp from_system_time() noexcept;

    // Accessors
    constexpr int64_t nanoseconds() const noexcept;
    constexpr int64_t microseconds() const noexcept;
    constexpr int64_t milliseconds() const noexcept;
    constexpr int64_t seconds() const noexcept;

    // Operators
    constexpr bool operator==(const Timestamp& other) const noexcept;
    constexpr bool operator<(const Timestamp& other) const noexcept;
    constexpr bool operator<=(const Timestamp& other) const noexcept;
    constexpr bool operator>=(const Timestamp& other) const noexcept;
    constexpr bool operator>(const Timestamp& other) const noexcept;
    Timestamp operator+(duration_type duration) const noexcept;
    duration_type operator-(const Timestamp& other) const noexcept;
};
```

**Example:**
```cpp
using namespace ipb::common;

auto ts = Timestamp::now();
auto system_ts = Timestamp::from_system_time();

std::cout << "Nanoseconds: " << ts.nanoseconds() << std::endl;
std::cout << "Microseconds: " << ts.microseconds() << std::endl;

// Duration arithmetic
auto later = ts + std::chrono::milliseconds(100);
auto elapsed = later - ts;
```

---

### Value

Lock-free value storage with type erasure, optimized for zero-copy operations.

**Header:** `<ipb/common/data_point.hpp>`

**Namespace:** `ipb::common`

```cpp
class Value {
public:
    enum class Type : uint8_t {
        EMPTY = 0,
        BOOL,
        INT8, INT16, INT32, INT64,
        UINT8, UINT16, UINT32, UINT64,
        FLOAT32, FLOAT64,
        STRING,
        BINARY
    };

    static constexpr size_t INLINE_SIZE = 56;

    // Constructors
    Value() noexcept;
    Value(const Value& other) noexcept;
    Value(Value&& other) noexcept;

    // Type-safe setters
    template<typename T>
    void set(T&& value) noexcept;

    // Zero-copy setters
    void set_string_view(std::string_view sv) noexcept;
    void set_binary(std::span<const uint8_t> data) noexcept;

    // Type-safe getters
    template<typename T>
    T get() const noexcept;

    // Zero-copy accessors
    std::string_view as_string_view() const noexcept;
    std::span<const uint8_t> as_binary() const noexcept;

    // Metadata
    Type type() const noexcept;
    size_t size() const noexcept;
    bool empty() const noexcept;

    // Comparison
    bool operator==(const Value& other) const noexcept;
    bool operator!=(const Value& other) const noexcept;

    // Serialization
    size_t serialized_size() const noexcept;
    void serialize(std::span<uint8_t> buffer) const noexcept;
    bool deserialize(std::span<const uint8_t> buffer) noexcept;
};
```

**Supported Types:**
- `bool`
- `int8_t`, `int16_t`, `int32_t`, `int64_t`
- `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`
- `float`, `double`
- `std::string_view` (for strings)
- `std::span<const uint8_t>` (for binary data)

**Example:**
```cpp
using namespace ipb::common;

Value v;

// Set numeric values
v.set(42);
int val = v.get<int32_t>();

// Set floating point
v.set(3.14159);
double pi = v.get<double>();

// Set string (zero-copy for small strings)
v.set_string_view("Hello, World!");
std::string_view str = v.as_string_view();

// Set binary data
std::vector<uint8_t> data = {0x01, 0x02, 0x03};
v.set_binary(data);
auto binary = v.as_binary();
```

---

### DataPoint

High-performance data point optimized for real-time systems with zero-copy value storage.

**Header:** `<ipb/common/data_point.hpp>`

**Namespace:** `ipb::common`

```cpp
class alignas(64) DataPoint {
public:
    static constexpr size_t MAX_INLINE_ADDRESS = 32;

    // Constructors
    DataPoint();
    explicit DataPoint(std::string_view address) noexcept;
    DataPoint(std::string_view address, Value value, uint16_t protocol_id = 0) noexcept;

    // Address management
    void set_address(std::string_view address) noexcept;
    std::string_view address() const noexcept;

    // Value management
    template<typename T>
    void set_value(T&& value) noexcept;
    void set_value(Value value) noexcept;
    const Value& value() const noexcept;
    Value& value() noexcept;

    // Metadata
    Timestamp timestamp() const noexcept;
    void set_timestamp(Timestamp ts) noexcept;

    uint16_t protocol_id() const noexcept;
    void set_protocol_id(uint16_t id) noexcept;

    Quality quality() const noexcept;
    void set_quality(Quality q) noexcept;

    uint32_t sequence_number() const noexcept;
    void set_sequence_number(uint32_t seq) noexcept;

    // Utility methods
    bool is_valid() const noexcept;
    bool is_stale(Timestamp current_time, std::chrono::nanoseconds max_age) const noexcept;

    // Serialization
    size_t serialized_size() const noexcept;
    void serialize(std::span<uint8_t> buffer) const noexcept;
    bool deserialize(std::span<const uint8_t> buffer) noexcept;

    // Hash and comparison
    size_t hash() const noexcept;
    bool operator==(const DataPoint& other) const noexcept;
};
```

**Example:**
```cpp
using namespace ipb::common;

// Create a data point
DataPoint dp("sensors/temperature/zone1");
dp.set_value(25.5);
dp.set_quality(Quality::GOOD);
dp.set_protocol_id(1);  // Modbus

// Check validity
if (dp.is_valid()) {
    std::cout << "Address: " << dp.address() << std::endl;
    std::cout << "Value: " << dp.value().get<double>() << std::endl;
}

// Check staleness
auto max_age = std::chrono::seconds(60);
if (!dp.is_stale(Timestamp::now(), max_age)) {
    // Data is fresh
}
```

---

### Quality

Quality indicator for data points.

**Header:** `<ipb/common/data_point.hpp>`

**Namespace:** `ipb::common`

```cpp
enum class Quality : uint8_t {
    GOOD = 0,           // Data is valid and current
    UNCERTAIN = 1,      // Data validity is uncertain
    BAD = 2,            // Data is known to be invalid
    STALE = 3,          // Data is old/outdated
    COMM_FAILURE = 4,   // Communication failure
    CONFIG_ERROR = 5,   // Configuration error
    NOT_CONNECTED = 6,  // Device not connected
    DEVICE_FAILURE = 7, // Device hardware failure
    SENSOR_FAILURE = 8, // Sensor failure
    LAST_KNOWN = 9,     // Last known good value
    INITIAL = 10,       // Initial/default value
    FORCED = 11         // Manually forced value
};
```

---

## Error Handling

### ErrorCode

Comprehensive error codes organized by category.

**Header:** `<ipb/common/error.hpp>`

**Namespace:** `ipb::common`

**Categories:**
- `0x00xx`: General/Common errors
- `0x01xx`: I/O and Connection errors
- `0x02xx`: Protocol errors
- `0x03xx`: Resource errors
- `0x04xx`: Configuration errors
- `0x05xx`: Security errors
- `0x06xx`: Routing errors
- `0x07xx`: Scheduling errors
- `0x08xx`: Serialization errors
- `0x09xx`: Validation errors
- `0x0Axx`: Platform-specific errors

**Helper Functions:**
```cpp
constexpr ErrorCategory get_category(ErrorCode code) noexcept;
constexpr bool is_success(ErrorCode code) noexcept;
constexpr bool is_transient(ErrorCode code) noexcept;
constexpr bool is_fatal(ErrorCode code) noexcept;
constexpr std::string_view error_name(ErrorCode code) noexcept;
```

---

### Result<T>

Modern Result type for error handling without exceptions.

**Header:** `<ipb/common/error.hpp>`

**Namespace:** `ipb::common`

```cpp
template<typename T>
class Result {
public:
    // Constructors
    Result(T value);                    // Success
    Result(ErrorCode code);             // Error
    Result(ErrorCode code, std::string_view message);
    Result(Error error);

    // Status
    bool is_success() const noexcept;
    bool is_error() const noexcept;
    explicit operator bool() const noexcept;

    // Value access
    T& value() & noexcept;
    const T& value() const& noexcept;
    T&& value() && noexcept;
    T value_or(T default_value) const&;

    // Error access
    ErrorCode code() const noexcept;
    const Error& error() const noexcept;
    const std::string& message() const noexcept;

    // Chaining
    Result& with_cause(Error cause);

    // Transform
    template<typename F>
    auto map(F&& func) const& -> Result<decltype(func(value()))>;
};

// Helper functions
template<typename T>
Result<T> ok(T value);

inline Result<void> ok();

template<typename T = void>
Result<T> err(ErrorCode code, std::string_view message = {});
```

**Example:**
```cpp
using namespace ipb::common;

Result<int> parse_config(const std::string& path) {
    if (path.empty()) {
        return err<int>(ErrorCode::CONFIG_MISSING, "Path cannot be empty");
    }
    // ... parse logic
    return ok(42);
}

auto result = parse_config("config.yaml");
if (result) {
    std::cout << "Value: " << result.value() << std::endl;
} else {
    std::cerr << "Error: " << result.message() << std::endl;
}

// Using value_or
int value = result.value_or(-1);

// Using map
auto doubled = result.map([](int v) { return v * 2; });
```

**Error Propagation Macros:**
```cpp
// Return early if result is error
IPB_TRY(some_function_returning_result());

// Return with custom message
IPB_TRY_MSG(operation(), "Failed to perform operation");

// Assign value or return error
IPB_TRY_ASSIGN(value, get_value());
```

---

## Network

### EndPoint

Network endpoint representation supporting multiple protocols.

**Header:** `<ipb/common/endpoint.hpp>`

**Namespace:** `ipb::common`

```cpp
class EndPoint {
public:
    enum class Protocol : uint8_t {
        TCP, UDP, UNIX_SOCKET, NAMED_PIPE, SERIAL,
        USB, BLUETOOTH, WEBSOCKET, HTTP, HTTPS, MQTT, COAP, CUSTOM
    };

    enum class SecurityLevel : uint8_t {
        NONE, BASIC_AUTH, TLS, MUTUAL_TLS, CERTIFICATE, TOKEN_BASED, CUSTOM
    };

    // Constructors
    EndPoint();
    EndPoint(Protocol protocol, std::string_view host, uint16_t port);
    EndPoint(Protocol protocol, std::string_view path);

    // Getters/Setters for host, port, path, protocol, security
    // ... (see header for full API)

    // Timeout configuration
    void set_connection_timeout(std::chrono::milliseconds timeout) noexcept;
    void set_read_timeout(std::chrono::milliseconds timeout) noexcept;
    void set_write_timeout(std::chrono::milliseconds timeout) noexcept;

    // Authentication
    void set_username(std::string_view username);
    void set_password(std::string_view password);
    void set_certificate_path(std::string_view cert_path);

    // URL conversion
    std::string to_url() const;
    static EndPoint from_url(std::string_view url);

    bool is_valid() const noexcept;
};
```

**Example:**
```cpp
using namespace ipb::common;

// TCP endpoint
EndPoint tcp_ep(EndPoint::Protocol::TCP, "192.168.1.100", 502);
tcp_ep.set_connection_timeout(std::chrono::seconds(5));

// MQTT endpoint with TLS
EndPoint mqtt_ep(EndPoint::Protocol::MQTT, "broker.example.com", 8883);
mqtt_ep.set_security_level(EndPoint::SecurityLevel::TLS);
mqtt_ep.set_certificate_path("/etc/certs/ca.crt");

std::cout << mqtt_ep.to_url() << std::endl;
// Output: mqtt://broker.example.com:8883
```

---

## Message Bus

### MessageBus

High-performance message bus for component communication.

**Header:** `<ipb/core/message_bus/message_bus.hpp>`

**Namespace:** `ipb::core`

```cpp
class MessageBus {
public:
    MessageBus();
    explicit MessageBus(const MessageBusConfig& config);

    // Lifecycle
    bool start();
    void stop();
    bool is_running() const noexcept;

    // Publishing
    bool publish(std::string_view topic, Message msg);
    bool publish(std::string_view topic, const common::DataPoint& data_point);
    bool publish_batch(std::string_view topic, std::span<const common::DataPoint> batch);
    bool publish_priority(std::string_view topic, Message msg, Message::Priority priority);
    bool publish_deadline(std::string_view topic, Message msg, common::Timestamp deadline);

    // Subscribing
    [[nodiscard]] Subscription subscribe(std::string_view topic_pattern, SubscriberCallback callback);
    [[nodiscard]] Subscription subscribe_filtered(
        std::string_view topic_pattern,
        std::function<bool(const Message&)> filter,
        SubscriberCallback callback);

    // Statistics
    const MessageBusStats& stats() const noexcept;
    void reset_stats();
};
```

**Example:**
```cpp
using namespace ipb::core;
using namespace ipb::common;

MessageBusConfig config;
config.default_buffer_size = 65536;
config.dispatcher_threads = 4;
config.priority_dispatch = true;

MessageBus bus(config);
bus.start();

// Subscribe with wildcard pattern
auto sub = bus.subscribe("sensors/*", [](const Message& msg) {
    std::cout << "Received from: " << msg.topic << std::endl;
});

// Subscribe with filter
auto filtered_sub = bus.subscribe_filtered(
    "sensors/temperature/*",
    [](const Message& msg) {
        return msg.payload.value().get<double>() > 30.0;  // Only high temps
    },
    [](const Message& msg) {
        std::cout << "High temperature alert!" << std::endl;
    }
);

// Publish
DataPoint dp("sensors/temperature/zone1");
dp.set_value(35.5);
bus.publish("sensors/temperature/zone1", dp);

// Check statistics
std::cout << "Messages published: " << bus.stats().messages_published << std::endl;

bus.stop();
```

---

## Real-Time Primitives

### SPSCRingBuffer

Lock-free ring buffer for single producer, single consumer scenarios.

**Header:** `<ipb/common/endpoint.hpp>`

**Namespace:** `ipb::common::rt`

```cpp
template<typename T, size_t Size>
class SPSCRingBuffer {
public:
    SPSCRingBuffer() noexcept;

    // Producer interface
    bool try_push(const T& item) noexcept;
    bool try_push(T&& item) noexcept;

    // Consumer interface
    bool try_pop(T& item) noexcept;

    // Status
    bool empty() const noexcept;
    bool full() const noexcept;
    size_t size() const noexcept;
    static constexpr size_t capacity() noexcept;
};
```

**Note:** Size must be a power of 2.

**Example:**
```cpp
using namespace ipb::common::rt;

SPSCRingBuffer<int, 1024> buffer;

// Producer thread
buffer.try_push(42);

// Consumer thread
int value;
if (buffer.try_pop(value)) {
    std::cout << "Got: " << value << std::endl;
}
```

---

### MemoryPool

Memory pool for zero-allocation operations in real-time contexts.

**Header:** `<ipb/common/endpoint.hpp>`

**Namespace:** `ipb::common::rt`

```cpp
template<typename T, size_t PoolSize>
class MemoryPool {
public:
    MemoryPool() noexcept;

    T* acquire() noexcept;
    void release(T* ptr) noexcept;

    size_t available() const noexcept;
    static constexpr size_t capacity() noexcept;
};
```

**Example:**
```cpp
using namespace ipb::common::rt;

struct SensorData {
    double value;
    uint64_t timestamp;
};

MemoryPool<SensorData, 1000> pool;

// Acquire memory
SensorData* data = pool.acquire();
if (data) {
    data->value = 25.5;
    data->timestamp = 123456789;

    // Use the data...

    // Release back to pool
    pool.release(data);
}
```

---

## Performance Characteristics

### Latency Targets

| Operation | P50 | P95 | P99 |
|-----------|-----|-----|-----|
| DataPoint creation | 45ns | 78ns | 125ns |
| Router processing | 12us | 25us | 45us |
| MQTT publish | 850us | 1.2ms | 2.1ms |
| Memory pool acquire | <10ns | <20ns | <50ns |

### Throughput Targets

| Component | Rate |
|-----------|------|
| Message Bus | >5M msg/s |
| Router | >2M msg/s |
| MQTT Sink | 50K msg/s |
| Full Pipeline | 45K msg/s |

---

## Thread Safety

- **Timestamp**: Thread-safe (immutable after construction)
- **Value**: Thread-safe for reads, not for concurrent writes
- **DataPoint**: Thread-safe for reads, not for concurrent writes
- **MessageBus**: Fully thread-safe
- **SPSCRingBuffer**: Thread-safe for single producer/single consumer
- **MemoryPool**: Thread-safe (lock-free)

---

## See Also

- [Getting Started Guide](GETTING_STARTED.md)
- [Configuration Reference](CONFIGURATION.md)
- [Architecture Document](../ARCHITECTURE.md)
