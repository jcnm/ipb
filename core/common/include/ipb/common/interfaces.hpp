#pragma once

#include "data_point.hpp"
#include "dataset.hpp"
#include "endpoint.hpp"
#include "error.hpp"  // For Result<T> and ErrorCode
#include <memory>
#include <functional>
#include <future>
#include <span>
#include <string_view>
#include <vector>
#include <chrono>

namespace ipb::common {

// ============================================================================
// BACKWARD COMPATIBILITY ALIASES
// ============================================================================

/**
 * @brief Legacy error code type alias
 * @deprecated Use ErrorCode instead
 *
 * Mapping from legacy ResultErrorCode to modern ErrorCode:
 * - SUCCESS           -> ErrorCode::SUCCESS
 * - INVALID_ARGUMENT  -> ErrorCode::INVALID_ARGUMENT
 * - TIMEOUT           -> ErrorCode::OPERATION_TIMEOUT
 * - CONNECTION_FAILED -> ErrorCode::CONNECTION_FAILED
 * - PROTOCOL_ERROR    -> ErrorCode::PROTOCOL_ERROR
 * - BUFFER_OVERFLOW   -> ErrorCode::BUFFER_OVERFLOW
 * - INSUFFICIENT_MEMORY -> ErrorCode::OUT_OF_MEMORY
 * - PERMISSION_DENIED -> ErrorCode::PERMISSION_DENIED
 * - DEVICE_NOT_FOUND  -> ErrorCode::DEVICE_NOT_FOUND
 * - OPERATION_CANCELLED -> ErrorCode::OPERATION_CANCELLED
 * - NOT_FOUND         -> ErrorCode::NOT_FOUND
 * - NOT_SUPPORTED     -> ErrorCode::NOT_IMPLEMENTED
 * - CONFIG_ERROR      -> ErrorCode::CONFIG_INVALID
 * - IO_ERROR          -> ErrorCode::READ_ERROR or ErrorCode::WRITE_ERROR
 * - INTERNAL_ERROR    -> ErrorCode::UNKNOWN_ERROR
 */
using ResultErrorCode = ErrorCode;

// Legacy error code values as inline constexpr for backward compatibility
namespace legacy_error {
    inline constexpr ErrorCode SUCCESS = ErrorCode::SUCCESS;
    inline constexpr ErrorCode INVALID_ARGUMENT = ErrorCode::INVALID_ARGUMENT;
    inline constexpr ErrorCode TIMEOUT = ErrorCode::OPERATION_TIMEOUT;
    inline constexpr ErrorCode CONNECTION_FAILED = ErrorCode::CONNECTION_FAILED;
    inline constexpr ErrorCode PROTOCOL_ERROR = ErrorCode::PROTOCOL_ERROR;
    inline constexpr ErrorCode BUFFER_OVERFLOW = ErrorCode::BUFFER_OVERFLOW;
    inline constexpr ErrorCode INSUFFICIENT_MEMORY = ErrorCode::OUT_OF_MEMORY;
    inline constexpr ErrorCode PERMISSION_DENIED = ErrorCode::PERMISSION_DENIED;
    inline constexpr ErrorCode DEVICE_NOT_FOUND = ErrorCode::DEVICE_NOT_FOUND;
    inline constexpr ErrorCode OPERATION_CANCELLED = ErrorCode::OPERATION_CANCELLED;
    inline constexpr ErrorCode NOT_FOUND = ErrorCode::NOT_FOUND;
    inline constexpr ErrorCode NOT_SUPPORTED = ErrorCode::NOT_IMPLEMENTED;
    inline constexpr ErrorCode CONFIG_ERROR = ErrorCode::CONFIG_INVALID;
    inline constexpr ErrorCode IO_ERROR = ErrorCode::WRITE_ERROR;
    inline constexpr ErrorCode INTERNAL_ERROR = ErrorCode::UNKNOWN_ERROR;
} // namespace legacy_error

/**
 * @brief Statistics for performance monitoring
 */
struct Statistics {
    uint64_t total_messages = 0;
    uint64_t successful_messages = 0;
    uint64_t failed_messages = 0;
    uint64_t total_bytes = 0;
    
    std::chrono::nanoseconds total_processing_time{0};
    std::chrono::nanoseconds min_processing_time{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_processing_time{0};
    
    Timestamp start_time;
    Timestamp last_update_time;
    
    // Calculated metrics
    double success_rate() const noexcept {
        return total_messages > 0 ? 
            static_cast<double>(successful_messages) / total_messages * 100.0 : 0.0;
    }
    
    double messages_per_second() const noexcept {
        auto duration = last_update_time - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        return seconds.count() > 0 ?
            static_cast<double>(total_messages) / seconds.count() : 0.0;
    }
    
    double average_processing_time_us() const noexcept {
        return total_messages > 0 ?
            static_cast<double>(total_processing_time.count()) / total_messages / 1000.0 : 0.0;
    }
    
    void reset() noexcept {
        *this = Statistics{};
        start_time = Timestamp::now();
        last_update_time = start_time;
    }
};

/**
 * @brief Metrics for sink monitoring
 */
struct SinkMetrics {
    std::string sink_id;
    uint64_t messages_sent = 0;
    uint64_t messages_failed = 0;
    uint64_t bytes_sent = 0;
    bool is_connected = false;
    bool is_healthy = true;
    std::chrono::nanoseconds avg_processing_time{0};
};

/**
 * @brief Base interface for IPB sinks (simplified version for inheritance)
 *
 * This interface provides the basic contract that sink implementations
 * should follow. Unlike IIPBSinkBase which is designed for type-erasure,
 * this interface is designed for direct inheritance.
 */
class ISink {
public:
    virtual ~ISink() = default;

    // Lifecycle management
    virtual Result<void> initialize(const std::string& config_path) = 0;
    virtual Result<void> start() = 0;
    virtual Result<void> stop() = 0;
    virtual Result<void> shutdown() = 0;

    // Data sending
    virtual Result<void> send_data_point(const DataPoint& data_point) = 0;
    virtual Result<void> send_data_set(const DataSet& data_set) = 0;

    // Status
    virtual bool is_connected() const = 0;
    virtual bool is_healthy() const = 0;

    // Metrics and info
    virtual SinkMetrics get_metrics() const = 0;
    virtual std::string get_sink_info() const = 0;
};

/**
 * @brief Configuration base class with validation
 */
class ConfigurationBase {
public:
    virtual ~ConfigurationBase() = default;
    
    // Validation interface
    virtual Result<void> validate() const = 0;
    
    // Serialization interface
    virtual std::string to_string() const = 0;
    virtual Result<void> from_string(std::string_view config) = 0;
    
    // Clone interface for type erasure
    virtual std::unique_ptr<ConfigurationBase> clone() const = 0;
};

/**
 * @brief Base interface for all IPB components
 */
class IIPBComponent {
public:
    virtual ~IIPBComponent() = default;
    
    // Lifecycle management
    virtual Result<void> start() = 0;
    virtual Result<void> stop() = 0;
    virtual bool is_running() const noexcept = 0;
    
    // Configuration management
    virtual Result<void> configure(const ConfigurationBase& config) = 0;
    virtual std::unique_ptr<ConfigurationBase> get_configuration() const = 0;
    
    // Statistics and monitoring
    virtual Statistics get_statistics() const noexcept = 0;
    virtual void reset_statistics() noexcept = 0;
    
    // Health check
    virtual bool is_healthy() const noexcept = 0;
    virtual std::string get_health_status() const = 0;
    
    // Component identification
    virtual std::string_view component_name() const noexcept = 0;
    virtual std::string_view component_version() const noexcept = 0;
};

/**
 * @brief Base interface for protocol sources
 */
class IProtocolSourceBase : public IIPBComponent {
public:
    // Data reading interface
    virtual Result<DataSet> read() = 0;
    virtual Result<DataSet> read_async() = 0;
    
    // Subscription interface for real-time data
    using DataCallback = std::function<void(DataSet)>;
    using ErrorCallback = std::function<void(ErrorCode, std::string_view)>;
    
    virtual Result<void> subscribe(DataCallback data_cb, ErrorCallback error_cb) = 0;
    virtual Result<void> unsubscribe() = 0;
    
    // Address space management
    virtual Result<void> add_address(std::string_view address) = 0;
    virtual Result<void> remove_address(std::string_view address) = 0;
    virtual std::vector<std::string> get_addresses() const = 0;
    
    // Connection management
    virtual Result<void> connect() = 0;
    virtual Result<void> disconnect() = 0;
    virtual bool is_connected() const noexcept = 0;
    
    // Protocol-specific information
    virtual uint16_t protocol_id() const noexcept = 0;
    virtual std::string_view protocol_name() const noexcept = 0;
};

/**
 * @brief Type-erased protocol source interface
 */
class IProtocolSource {
public:
    // Constructor with type erasure
    template<typename T>
    IProtocolSource(std::unique_ptr<T> impl) 
        : impl_(std::move(impl)) {
        static_assert(std::is_base_of_v<IProtocolSourceBase, T>, 
                     "T must inherit from IProtocolSourceBase");
    }
    
    // Move semantics
    IProtocolSource(IProtocolSource&&) = default;
    IProtocolSource& operator=(IProtocolSource&&) = default;
    
    // Disable copy
    IProtocolSource(const IProtocolSource&) = delete;
    IProtocolSource& operator=(const IProtocolSource&) = delete;
    
    // Forward all interface methods
    Result<DataSet> read() { return impl_->read(); }
    Result<DataSet> read_async() { return impl_->read_async(); }
    
    Result<void> subscribe(IProtocolSourceBase::DataCallback data_cb, 
                      IProtocolSourceBase::ErrorCallback error_cb) {
        return impl_->subscribe(std::move(data_cb), std::move(error_cb));
    }
    
    Result<void> unsubscribe() { return impl_->unsubscribe(); }
    
    Result<void> add_address(std::string_view address) { return impl_->add_address(address); }
    Result<void> remove_address(std::string_view address) { return impl_->remove_address(address); }
    std::vector<std::string> get_addresses() const { return impl_->get_addresses(); }
    
    Result<void> connect() { return impl_->connect(); }
    Result<void> disconnect() { return impl_->disconnect(); }
    bool is_connected() const noexcept { return impl_->is_connected(); }
    
    uint16_t protocol_id() const noexcept { return impl_->protocol_id(); }
    std::string_view protocol_name() const noexcept { return impl_->protocol_name(); }
    
    // IIPBComponent interface
    Result<void> start() { return impl_->start(); }
    Result<void> stop() { return impl_->stop(); }
    bool is_running() const noexcept { return impl_->is_running(); }
    
    Result<void> configure(const ConfigurationBase& config) { return impl_->configure(config); }
    std::unique_ptr<ConfigurationBase> get_configuration() const { return impl_->get_configuration(); }
    
    Statistics get_statistics() const noexcept { return impl_->get_statistics(); }
    void reset_statistics() noexcept { impl_->reset_statistics(); }
    
    bool is_healthy() const noexcept { return impl_->is_healthy(); }
    std::string get_health_status() const { return impl_->get_health_status(); }
    
    std::string_view component_name() const noexcept { return impl_->component_name(); }
    std::string_view component_version() const noexcept { return impl_->component_version(); }

private:
    std::unique_ptr<IProtocolSourceBase> impl_;
};

/**
 * @brief Base interface for data sinks
 */
class IIPBSinkBase : public IIPBComponent {
public:
    // Data writing interface
    virtual Result<void> write(const DataPoint& data_point) = 0;
    virtual Result<void> write_batch(std::span<const DataPoint> data_points) = 0;
    virtual Result<void> write_dataset(const DataSet& dataset) = 0;
    
    // Asynchronous writing
    virtual std::future<Result<void>> write_async(const DataPoint& data_point) = 0;
    virtual std::future<Result<void>> write_batch_async(std::span<const DataPoint> data_points) = 0;
    
    // Flow control
    virtual Result<void> flush() = 0;
    virtual size_t pending_count() const noexcept = 0;
    virtual bool can_accept_data() const noexcept = 0;
    
    // Sink-specific information
    virtual std::string_view sink_type() const noexcept = 0;
    virtual size_t max_batch_size() const noexcept = 0;
};

/**
 * @brief Type-erased sink interface
 */
class IIPBSink {
public:
    // Constructor with type erasure
    template<typename T>
    IIPBSink(std::unique_ptr<T> impl) 
        : impl_(std::move(impl)) {
        static_assert(std::is_base_of_v<IIPBSinkBase, T>, 
                     "T must inherit from IIPBSinkBase");
    }
    
    // Move semantics
    IIPBSink(IIPBSink&&) = default;
    IIPBSink& operator=(IIPBSink&&) = default;
    
    // Disable copy
    IIPBSink(const IIPBSink&) = delete;
    IIPBSink& operator=(const IIPBSink&) = delete;
    
    // Forward all interface methods
    Result<void> write(const DataPoint& data_point) { return impl_->write(data_point); }
    Result<void> write_batch(std::span<const DataPoint> data_points) { 
        return impl_->write_batch(data_points); 
    }
    Result<void> write_dataset(const DataSet& dataset) { return impl_->write_dataset(dataset); }
    
    std::future<Result<void>> write_async(const DataPoint& data_point) { 
        return impl_->write_async(data_point); 
    }
    std::future<Result<void>> write_batch_async(std::span<const DataPoint> data_points) { 
        return impl_->write_batch_async(data_points); 
    }
    
    Result<void> flush() { return impl_->flush(); }
    size_t pending_count() const noexcept { return impl_->pending_count(); }
    bool can_accept_data() const noexcept { return impl_->can_accept_data(); }
    
    std::string_view sink_type() const noexcept { return impl_->sink_type(); }
    size_t max_batch_size() const noexcept { return impl_->max_batch_size(); }
    
    // IIPBComponent interface
    Result<void> start() { return impl_->start(); }
    Result<void> stop() { return impl_->stop(); }
    bool is_running() const noexcept { return impl_->is_running(); }
    
    Result<void> configure(const ConfigurationBase& config) { return impl_->configure(config); }
    std::unique_ptr<ConfigurationBase> get_configuration() const { return impl_->get_configuration(); }
    
    Statistics get_statistics() const noexcept { return impl_->get_statistics(); }
    void reset_statistics() noexcept { impl_->reset_statistics(); }
    
    bool is_healthy() const noexcept { return impl_->is_healthy(); }
    std::string get_health_status() const { return impl_->get_health_status(); }
    
    std::string_view component_name() const noexcept { return impl_->component_name(); }
    std::string_view component_version() const noexcept { return impl_->component_version(); }

private:
    std::unique_ptr<IIPBSinkBase> impl_;
};

/**
 * @brief Address space interface for protocol discovery
 */
class IAddressSpace {
public:
    virtual ~IAddressSpace() = default;
    
    // Address discovery
    virtual Result<std::vector<std::string>> discover_addresses() = 0;
    virtual Result<std::vector<std::string>> browse_children(std::string_view parent_address) = 0;
    
    // Address validation
    virtual bool is_valid_address(std::string_view address) const noexcept = 0;
    virtual Result<std::string> normalize_address(std::string_view address) const = 0;
    
    // Address metadata
    virtual Result<Value::Type> get_address_type(std::string_view address) const = 0;
    virtual Result<std::string> get_address_description(std::string_view address) const = 0;
    virtual Result<bool> is_address_readable(std::string_view address) const = 0;
    virtual Result<bool> is_address_writable(std::string_view address) const = 0;
};

/**
 * @brief Factory interface for creating protocol sources and sinks
 */
class IIPBFactory {
public:
    virtual ~IIPBFactory() = default;
    
    // Protocol source creation
    virtual Result<IProtocolSource> create_protocol_source(
        std::string_view protocol_name,
        const ConfigurationBase& config) = 0;
    
    // Sink creation
    virtual Result<IIPBSink> create_sink(
        std::string_view sink_type,
        const ConfigurationBase& config) = 0;
    
    // Capability queries
    virtual std::vector<std::string> supported_protocols() const = 0;
    virtual std::vector<std::string> supported_sinks() const = 0;
    
    // Version information
    virtual std::string_view factory_name() const noexcept = 0;
    virtual std::string_view factory_version() const noexcept = 0;
};

} // namespace ipb::common

