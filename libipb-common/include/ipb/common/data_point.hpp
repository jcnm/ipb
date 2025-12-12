#pragma once

#include <chrono>
#include <string_view>
#include <span>
#include <variant>
#include <atomic>
#include <memory>
#include <cstring>
#include <type_traits>

namespace ipb::common {

/**
 * @brief High-performance timestamp with nanosecond precision.
 *
 * Optimized for real-time systems with minimal overhead. This class
 * uses `std::chrono::steady_clock` as its primary clock source, ensuring monotonic time
 * progression. It also supports creation from system clock via `from_system_time()`.
 */
class Timestamp {
public:
    // Define the duration type and clock used
    using duration_type = std::chrono::nanoseconds;
    using clock_type = std::chrono::steady_clock;

    /**
     * @brief Default constructor.
     *
     * Creates a timestamp at epoch (0 nanoseconds).
     */
    constexpr Timestamp() noexcept : ns_since_epoch_(0) {}

    /**
     * @brief Constructor from a duration of nanoseconds.
     *
     * @param ns The duration in nanoseconds since the epoch.
     */
    explicit Timestamp(duration_type ns) noexcept : ns_since_epoch_(ns.count()) {}

    /**
     * @brief Returns the current timestamp using a monotonic clock.
     *
     * This function uses `std::chrono::steady_clock` to ensure consistent time progression.
     */
    static Timestamp now() noexcept {
        return Timestamp(clock_type::now().time_since_epoch());
    }

    /**
     * @brief Returns the current timestamp using the system clock (`std::chrono::system_clock`).
     *
     * @note: The timestamp is derived from system time, which may be subject to changes
     * (e.g., via NTP or user input).
     *
     * @param system_now: Captures current time from the system clock.
     * @param system_ns: The duration in nanoseconds since epoch, converted from `system_now`.
     * @return A new Timestamp object based on system time.
     */
    static Timestamp from_system_time() noexcept {
        // 1. Capture current system time (can be adjusted)
        auto system_now = std::chrono::system_clock::now();

        // // 2. Capture current time from monotonic clock (not used in this implementation)
        // auto steady_now = clock_type::now();

        // 3. Convert system time duration to nanoseconds
        auto system_ns = std::chrono::duration_cast<duration_type>(system_now.time_since_epoch());

        // 4. Return a new timestamp constructed from system time duration
        return Timestamp(system_ns);
    }

    /**
     * @brief Returns the number of nanoseconds since epoch.
     */
    constexpr int64_t nanoseconds() const noexcept { return ns_since_epoch_; }

    /**
     * @brief Returns the number of microseconds since epoch.
     */
    constexpr int64_t microseconds() const noexcept { return ns_since_epoch_ / 1000; }

    /**
     * @brief Returns the number of milliseconds since epoch.
     */
    constexpr int64_t milliseconds() const noexcept { return ns_since_epoch_ / 1000000; }

    /**
     * @brief Returns the number of seconds since epoch.
     */
    constexpr int64_t seconds() const noexcept { return ns_since_epoch_ / 1000000000; }

    /**
     * @brief Equality operator.
     */
    constexpr bool operator==(const Timestamp& other) const noexcept {
        return ns_since_epoch_ == other.ns_since_epoch_;
    }

    /**
     * @brief Less-than operator.
     */
    constexpr bool operator<(const Timestamp& other) const noexcept {
        return ns_since_epoch_ < other.ns_since_epoch_;
    }
    constexpr bool operator<=(const Timestamp& other) const noexcept {
        return ns_since_epoch_ <= other.ns_since_epoch_;
    }

    constexpr bool operator>=(const Timestamp& other) const noexcept {
        return ns_since_epoch_ >= other.ns_since_epoch_;
    }

    constexpr bool operator>(const Timestamp& other) const noexcept {
        return ns_since_epoch_ > other.ns_since_epoch_;
    }

    /**
     * @brief Adds a duration to this timestamp.
     */
    Timestamp operator+(duration_type duration) const noexcept {
        return Timestamp(duration_type(ns_since_epoch_ + duration.count()));
    }

    /**
     * @brief Subtracts two timestamps to get a duration.
     */
    duration_type operator-(const Timestamp& other) const noexcept {
        return duration_type(ns_since_epoch_ - other.ns_since_epoch_);
    }

    friend std::ostream& operator<<(std::ostream& os, const Timestamp& ts) {
        os << ts.nanoseconds() << " ns";
        return os;
    }

private:
    /**
     * @brief Stores the number of nanoseconds since epoch.
     *
     * This is a 64-bit integer, providing enough range for timestamps covering
     * many years with nanosecond precision.
     */
    int64_t ns_since_epoch_;
};

/**
 * @brief Lock-free value storage with type erasure
 * 
 * Optimized for zero-copy operations and real-time performance
 */
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
    
    // Maximum inline storage size (cache line friendly)
    static constexpr size_t INLINE_SIZE = 56;
    
    Value() noexcept : type_(Type::EMPTY), size_(0) {}
    
    // Copy constructor with zero-copy optimization
    Value(const Value& other) noexcept {
        copy_from(other);
    }
    
    // Move constructor
    Value(Value&& other) noexcept {
        move_from(std::move(other));
    }
    
    // Assignment operators
    Value& operator=(const Value& other) noexcept {
        if (this != &other) {
            cleanup();
            copy_from(other);
        }
        return *this;
    }
    
    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            cleanup();
            move_from(std::move(other));
        }
        return *this;
    }
    
    ~Value() noexcept {
        cleanup();
    }
    
    // Type-safe setters with perfect forwarding
    template<typename T>
    void set(T&& value) noexcept {
        static_assert(is_supported_type_v<std::decay_t<T>>, "Unsupported type");
        cleanup();
        set_impl(std::forward<T>(value));
    }
    
    // Zero-copy string view setter
    void set_string_view(std::string_view sv) noexcept {
        cleanup();
        type_ = Type::STRING;
        size_ = sv.size();
        
        if (size_ <= INLINE_SIZE) {
            std::memcpy(inline_data_, sv.data(), size_);
        } else {
            external_data_ = std::make_unique<uint8_t[]>(size_);
            std::memcpy(external_data_.get(), sv.data(), size_);
        }
    }
    
    // Zero-copy binary data setter
    void set_binary(std::span<const uint8_t> data) noexcept {
        cleanup();
        type_ = Type::BINARY;
        size_ = data.size();
        
        if (size_ <= INLINE_SIZE) {
            std::memcpy(inline_data_, data.data(), size_);
        } else {
            external_data_ = std::make_unique<uint8_t[]>(size_);
            std::memcpy(external_data_.get(), data.data(), size_);
        }
    }
    
    // Type-safe getters
    template<typename T>
    T get() const noexcept {
        static_assert(is_supported_type_v<T>, "Unsupported type");
        return get_impl<T>();
    }
    
    // Zero-copy accessors
    std::string_view as_string_view() const noexcept {
        if (type_ != Type::STRING) return {};
        const char* data = size_ <= INLINE_SIZE ? 
            reinterpret_cast<const char*>(inline_data_) :
            reinterpret_cast<const char*>(external_data_.get());
        return std::string_view(data, size_);
    }
    
    std::span<const uint8_t> as_binary() const noexcept {
        if (type_ != Type::BINARY) return {};
        const uint8_t* data = size_ <= INLINE_SIZE ? 
            inline_data_ : external_data_.get();
        return std::span<const uint8_t>(data, size_);
    }
    
    // Metadata accessors
    Type type() const noexcept { return type_; }
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return type_ == Type::EMPTY; }

    // Comparison operators
    bool operator==(const Value& other) const noexcept {
        if (type_ != other.type_) return false;
        if (type_ == Type::EMPTY) return true;

        switch (type_) {
            case Type::BOOL: return get<bool>() == other.get<bool>();
            case Type::INT8: return get<int8_t>() == other.get<int8_t>();
            case Type::INT16: return get<int16_t>() == other.get<int16_t>();
            case Type::INT32: return get<int32_t>() == other.get<int32_t>();
            case Type::INT64: return get<int64_t>() == other.get<int64_t>();
            case Type::UINT8: return get<uint8_t>() == other.get<uint8_t>();
            case Type::UINT16: return get<uint16_t>() == other.get<uint16_t>();
            case Type::UINT32: return get<uint32_t>() == other.get<uint32_t>();
            case Type::UINT64: return get<uint64_t>() == other.get<uint64_t>();
            case Type::FLOAT32: return get<float>() == other.get<float>();
            case Type::FLOAT64: return get<double>() == other.get<double>();
            case Type::STRING: return as_string_view() == other.as_string_view();
            case Type::BINARY: {
                auto a = as_binary();
                auto b = other.as_binary();
                return a.size() == b.size() &&
                       std::memcmp(a.data(), b.data(), a.size()) == 0;
            }
            default: return false;
        }
    }

    bool operator!=(const Value& other) const noexcept {
        return !(*this == other);
    }
    
    // Serialization support
    size_t serialized_size() const noexcept {
        return sizeof(Type) + sizeof(size_t) + size_;
    }
    
    void serialize(std::span<uint8_t> buffer) const noexcept;
    bool deserialize(std::span<const uint8_t> buffer) noexcept;

private:
    Type type_;
    size_t size_;
    
    union {
        uint8_t inline_data_[INLINE_SIZE];
        std::unique_ptr<uint8_t[]> external_data_;
    };
    
    template<typename T>
    static constexpr bool is_supported_type_v = 
        std::is_same_v<T, bool> ||
        std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> ||
        std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
        std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> ||
        std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t> ||
        std::is_same_v<T, float> || std::is_same_v<T, double>;
    
    template<typename T>
    void set_impl(T&& value) noexcept;
    
    template<typename T>
    T get_impl() const noexcept;
    
    void copy_from(const Value& other) noexcept;
    void move_from(Value&& other) noexcept;
    void cleanup() noexcept;
};

/**
 * @brief Quality indicator for data points
 */
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

/**
 * @brief High-performance data point optimized for real-time systems
 * 
 * Features:
 * - Zero-copy value storage
 * - Lock-free operations where possible
 * - Cache-friendly memory layout
 * - Minimal allocation overhead
 * - Thread-safe read operations
 */
class alignas(64) DataPoint {
public:
    // Maximum address length for inline storage
    static constexpr size_t MAX_INLINE_ADDRESS = 32;
    
    DataPoint(){
        set_address("N/A");
        timestamp_ = Timestamp::now();
    };
    
    // Constructor with address
    explicit DataPoint(std::string_view address) noexcept {
        set_address(address);
        timestamp_ = Timestamp::now();
    }
    
    // Constructor with full initialization
    DataPoint(std::string_view address, Value value, uint16_t protocol_id = 0) noexcept
        : value_(std::move(value)), protocol_id_(protocol_id), quality_(Quality::GOOD) {
        set_address(address);
        timestamp_ = Timestamp::now();
    }
    
    // Copy constructor (thread-safe)
    DataPoint(const DataPoint& other) noexcept {
        copy_from(other);
    }
    
    // Move constructor
    DataPoint(DataPoint&& other) noexcept {
        move_from(std::move(other));
    }
    
    // Assignment operators
    DataPoint& operator=(const DataPoint& other) noexcept {
        if (this != &other) {
            copy_from(other);
        }
        return *this;
    }
    
    DataPoint& operator=(DataPoint&& other) noexcept {
        if (this != &other) {
            move_from(std::move(other));
        }
        return *this;
    }
    
    // Destructor
    ~DataPoint() {
        // Cleanup is handled in move_from and copy_from
    };
    
    // Address management (zero-copy when possible)
    void set_address(std::string_view address) noexcept {
        address_size_ = std::min(address.size(), static_cast<size_t>(UINT16_MAX));
        
        if (address_size_ <= MAX_INLINE_ADDRESS) {
            std::memcpy(inline_address_, address.data(), address_size_);
            external_address_.reset();
        } else {
            external_address_ = std::make_unique<char[]>(address_size_);
            std::memcpy(external_address_.get(), address.data(), address_size_);
        }
    }
    
    std::string_view address() const noexcept {
        const char* data = address_size_ <= MAX_INLINE_ADDRESS ?
            inline_address_ : external_address_.get();
        return std::string_view(data, address_size_);
    }
    
    // Value management
    template<typename T>
    void set_value(T&& value) noexcept {
        value_.set(std::forward<T>(value));
        timestamp_ = Timestamp::now();
        quality_ = Quality::GOOD;
    }
    
    void set_value(Value value) noexcept {
        value_ = std::move(value);
        timestamp_ = Timestamp::now();
        quality_ = Quality::GOOD;
    }
    
    const Value& value() const noexcept { return value_; }
    Value& value() noexcept { return value_; }
    
    // Metadata accessors
    Timestamp timestamp() const noexcept { return timestamp_; }
    void set_timestamp(Timestamp ts) noexcept { timestamp_ = ts; }
    
    uint16_t protocol_id() const noexcept { return protocol_id_; }
    void set_protocol_id(uint16_t id) noexcept { protocol_id_ = id; }
    
    Quality quality() const noexcept { return quality_; }
    void set_quality(Quality q) noexcept { quality_ = q; }
    
    uint32_t sequence_number() const noexcept { return sequence_number_; }
    void set_sequence_number(uint32_t seq) noexcept { sequence_number_ = seq; }

    // Backward-compatible accessors (deprecated, use the short names above)
    std::string_view get_address() const noexcept { return address(); }
    Timestamp get_timestamp() const noexcept { return timestamp(); }
    uint16_t get_protocol_id() const noexcept { return protocol_id(); }
    Quality get_quality() const noexcept { return quality(); }

    // Backward-compatible value accessor returning optional-like interface
    struct OptionalValueWrapper {
        const Value* value_;
        bool has_value() const noexcept { return value_ && !value_->empty(); }
        const Value& value() const noexcept { return *value_; }
    };
    OptionalValueWrapper get_value() const noexcept { return OptionalValueWrapper{&value_}; }

    // Utility methods
    bool is_valid() const noexcept {
        return quality_ == Quality::GOOD || quality_ == Quality::UNCERTAIN;
    }
    
    bool is_stale(Timestamp current_time, std::chrono::nanoseconds max_age) const noexcept {
        return (current_time - timestamp_) > max_age;
    }
    
    // Serialization support
    size_t serialized_size() const noexcept {
        return sizeof(uint16_t) + address_size_ +  // address
               value_.serialized_size() +          // value
               sizeof(Timestamp) +                 // timestamp
               sizeof(uint16_t) +                  // protocol_id
               sizeof(Quality) +                   // quality
               sizeof(uint32_t);                   // sequence_number
    }
    
    void serialize(std::span<uint8_t> buffer) const noexcept;
    bool deserialize(std::span<const uint8_t> buffer) noexcept;
    
    // Hash support for containers
    size_t hash() const noexcept;
    
    // Comparison operators
    bool operator==(const DataPoint& other) const noexcept {
        return address() == other.address() && 
               protocol_id_ == other.protocol_id_;
    }

private:
    // Value storage
    Value value_;
    
    // Timestamp with nanosecond precision
    Timestamp timestamp_;
    
    // Address storage (optimized for small addresses)
    uint16_t address_size_ = 0;
    union {
        char inline_address_[MAX_INLINE_ADDRESS];
        std::unique_ptr<char[]> external_address_;
    };
    
    // Metadata
    uint16_t protocol_id_ = 0;
    Quality quality_ = Quality::INITIAL;
    uint32_t sequence_number_ = 0;
    
    void copy_from(const DataPoint& other) noexcept;
    void move_from(DataPoint&& other) noexcept;
};

/**
 * @brief Raw message container for zero-copy protocol handling
 */
class RawMessage {
public:
    RawMessage() noexcept = default;
    
    // Constructor with data span (zero-copy)
    explicit RawMessage(std::span<const uint8_t> data) noexcept
        : data_(data), owns_data_(false) {}
    
    // Constructor with owned data
    explicit RawMessage(std::vector<uint8_t> data) noexcept
        : owned_data_(std::move(data)), owns_data_(true) {
        data_ = std::span<const uint8_t>(owned_data_);
    }
    
    // Move constructor
    RawMessage(RawMessage&& other) noexcept
        : data_(other.data_), owned_data_(std::move(other.owned_data_)), 
          owns_data_(other.owns_data_), protocol_id_(other.protocol_id_),
          timestamp_(other.timestamp_) {
        other.data_ = {};
        other.owns_data_ = false;
    }
    
    // Move assignment
    RawMessage& operator=(RawMessage&& other) noexcept {
        if (this != &other) {
            data_ = other.data_;
            owned_data_ = std::move(other.owned_data_);
            owns_data_ = other.owns_data_;
            protocol_id_ = other.protocol_id_;
            timestamp_ = other.timestamp_;
            
            other.data_ = {};
            other.owns_data_ = false;
        }
        return *this;
    }
    
    // Disable copy operations to enforce move semantics
    RawMessage(const RawMessage&) = delete;
    RawMessage& operator=(const RawMessage&) = delete;
    
    // Data access
    std::span<const uint8_t> data() const noexcept { return data_; }
    size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }
    
    // Metadata
    uint16_t protocol_id() const noexcept { return protocol_id_; }
    void set_protocol_id(uint16_t id) noexcept { protocol_id_ = id; }
    
    Timestamp timestamp() const noexcept { return timestamp_; }
    void set_timestamp(Timestamp ts) noexcept { timestamp_ = ts; }
    
    // Ownership info
    bool owns_data() const noexcept { return owns_data_; }

private:
    std::span<const uint8_t> data_;
    std::vector<uint8_t> owned_data_;
    bool owns_data_ = false;
    uint16_t protocol_id_ = 0;
    Timestamp timestamp_;
};

} // namespace ipb::common

// Hash specializations for standard containers
namespace std {
    template<>
    struct hash<ipb::common::DataPoint> {
        size_t operator()(const ipb::common::DataPoint& dp) const noexcept {
            return dp.hash();
        }
    };
}

