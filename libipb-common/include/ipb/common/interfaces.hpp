#pragma once

#include "data_point.hpp"
#include "dataset.hpp"
#include "endpoint.hpp"
#include <memory>
#include <functional>
#include <future>
#include <span>
#include <string_view>
#include <vector>
#include <chrono>

namespace ipb::common {

/**
 * @brief Result type for operations with error handling
 */
template<typename T = void>
class Result {
public:
    enum class ErrorCode : uint32_t {
        SUCCESS = 0,
        INVALID_ARGUMENT,
        TIMEOUT,
        CONNECTION_FAILED,
        PROTOCOL_ERROR,
        BUFFER_OVERFLOW,
        INSUFFICIENT_MEMORY,
        PERMISSION_DENIED,
        DEVICE_NOT_FOUND,
        OPERATION_CANCELLED,
        INTERNAL_ERROR
    };
    
    // Success constructor
    Result() noexcept : error_code_(ErrorCode::SUCCESS) {}
    
    // Success constructor with value
    template<typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    Result(U&& value) noexcept : value_(std::forward<U>(value)), error_code_(ErrorCode::SUCCESS) {}
    
    // Error constructor
    Result(ErrorCode error, std::string_view message = {}) noexcept 
        : error_code_(error) {
        if (!message.empty()) {
            error_message_ = std::string(message);
        }
    }
    
    // Status checks
    bool is_success() const noexcept { return error_code_ == ErrorCode::SUCCESS; }
    bool is_error() const noexcept { return error_code_ != ErrorCode::SUCCESS; }
    
    ErrorCode error_code() const noexcept { return error_code_; }
    const std::string& error_message() const noexcept { return error_message_; }
    
    // Value access (only for non-void types)
    template<typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    const U& value() const& noexcept { return value_; }
    
    template<typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U& value() & noexcept { return value_; }
    
    template<typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U&& value() && noexcept { return std::move(value_); }
    
    // Conversion operators
    explicit operator bool() const noexcept { return is_success(); }

private:
    [[no_unique_address]] T value_{};
    ErrorCode error_code_;
    std::string error_message_;
};

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
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::nanoseconds(duration.nanoseconds()));
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
 * @brief Configuration base class with validation
 */
class ConfigurationBase {
public:
    virtual ~ConfigurationBase() = default;
    
    // Validation interface
    virtual Result<> validate() const = 0;
    
    // Serialization interface
    virtual std::string to_string() const = 0;
    virtual Result<> from_string(std::string_view config) = 0;
    
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
    virtual Result<> start() = 0;
    virtual Result<> stop() = 0;
    virtual bool is_running() const noexcept = 0;
    
    // Configuration management
    virtual Result<> configure(const ConfigurationBase& config) = 0;
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
    using ErrorCallback = std::function<void(Result<>::ErrorCode, std::string_view)>;
    
    virtual Result<> subscribe(DataCallback data_cb, ErrorCallback error_cb) = 0;
    virtual Result<> unsubscribe() = 0;
    
    // Address space management
    virtual Result<> add_address(std::string_view address) = 0;
    virtual Result<> remove_address(std::string_view address) = 0;
    virtual std::vector<std::string> get_addresses() const = 0;
    
    // Connection management
    virtual Result<> connect() = 0;
    virtual Result<> disconnect() = 0;
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
    
    Result<> subscribe(IProtocolSourceBase::DataCallback data_cb, 
                      IProtocolSourceBase::ErrorCallback error_cb) {
        return impl_->subscribe(std::move(data_cb), std::move(error_cb));
    }
    
    Result<> unsubscribe() { return impl_->unsubscribe(); }
    
    Result<> add_address(std::string_view address) { return impl_->add_address(address); }
    Result<> remove_address(std::string_view address) { return impl_->remove_address(address); }
    std::vector<std::string> get_addresses() const { return impl_->get_addresses(); }
    
    Result<> connect() { return impl_->connect(); }
    Result<> disconnect() { return impl_->disconnect(); }
    bool is_connected() const noexcept { return impl_->is_connected(); }
    
    uint16_t protocol_id() const noexcept { return impl_->protocol_id(); }
    std::string_view protocol_name() const noexcept { return impl_->protocol_name(); }
    
    // IIPBComponent interface
    Result<> start() { return impl_->start(); }
    Result<> stop() { return impl_->stop(); }
    bool is_running() const noexcept { return impl_->is_running(); }
    
    Result<> configure(const ConfigurationBase& config) { return impl_->configure(config); }
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
    virtual Result<> write(const DataPoint& data_point) = 0;
    virtual Result<> write_batch(std::span<const DataPoint> data_points) = 0;
    virtual Result<> write_dataset(const DataSet& dataset) = 0;
    
    // Asynchronous writing
    virtual std::future<Result<>> write_async(const DataPoint& data_point) = 0;
    virtual std::future<Result<>> write_batch_async(std::span<const DataPoint> data_points) = 0;
    
    // Flow control
    virtual Result<> flush() = 0;
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
    Result<> write(const DataPoint& data_point) { return impl_->write(data_point); }
    Result<> write_batch(std::span<const DataPoint> data_points) { 
        return impl_->write_batch(data_points); 
    }
    Result<> write_dataset(const DataSet& dataset) { return impl_->write_dataset(dataset); }
    
    std::future<Result<>> write_async(const DataPoint& data_point) { 
        return impl_->write_async(data_point); 
    }
    std::future<Result<>> write_batch_async(std::span<const DataPoint> data_points) { 
        return impl_->write_batch_async(data_points); 
    }
    
    Result<> flush() { return impl_->flush(); }
    size_t pending_count() const noexcept { return impl_->pending_count(); }
    bool can_accept_data() const noexcept { return impl_->can_accept_data(); }
    
    std::string_view sink_type() const noexcept { return impl_->sink_type(); }
    size_t max_batch_size() const noexcept { return impl_->max_batch_size(); }
    
    // IIPBComponent interface
    Result<> start() { return impl_->start(); }
    Result<> stop() { return impl_->stop(); }
    bool is_running() const noexcept { return impl_->is_running(); }
    
    Result<> configure(const ConfigurationBase& config) { return impl_->configure(config); }
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

