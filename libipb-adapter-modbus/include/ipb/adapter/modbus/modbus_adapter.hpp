#pragma once

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/endpoint.hpp"
#include <modbus/modbus.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace ipb::adapter::modbus {

/**
 * @brief Modbus register types
 */
enum class RegisterType : uint8_t {
    COIL = 0,           // 0x (Read/Write)
    DISCRETE_INPUT,     // 1x (Read Only)
    INPUT_REGISTER,     // 3x (Read Only)
    HOLDING_REGISTER    // 4x (Read/Write)
};

/**
 * @brief Modbus data types
 */
enum class DataType : uint8_t {
    BOOL = 0,
    INT16,
    UINT16,
    INT32,
    UINT32,
    INT64,
    UINT64,
    FLOAT32,
    FLOAT64,
    STRING
};

/**
 * @brief Modbus address specification
 */
struct ModbusAddress {
    uint8_t slave_id = 1;
    RegisterType register_type = RegisterType::HOLDING_REGISTER;
    uint16_t start_address = 0;
    uint16_t count = 1;
    DataType data_type = DataType::UINT16;
    std::string name;
    
    // Parse from string format: "slave:type:address:count:datatype"
    // Example: "1:HR:40001:2:FLOAT32"
    static ModbusAddress parse(std::string_view address_str);
    
    // Convert to string format
    std::string to_string() const;
    
    // Validation
    bool is_valid() const noexcept;
    
    // Comparison
    bool operator==(const ModbusAddress& other) const noexcept;
    size_t hash() const noexcept;
};

/**
 * @brief Modbus adapter configuration
 */
class ModbusAdapterConfig : public ipb::common::ConfigurationBase {
public:
    // Connection settings
    ipb::common::EndPoint endpoint;
    std::chrono::milliseconds connection_timeout{5000};
    std::chrono::milliseconds response_timeout{1000};
    uint32_t max_retries = 3;
    std::chrono::milliseconds retry_delay{100};
    
    // Protocol settings
    bool enable_recovery = true;
    std::chrono::milliseconds recovery_timeout{10000};
    uint32_t max_pdu_length = 253;
    
    // Performance settings
    uint32_t max_batch_size = 100;
    std::chrono::milliseconds polling_interval{100};
    bool enable_async_polling = true;
    uint32_t worker_thread_count = 1;
    
    // Real-time settings
    bool enable_realtime_priority = false;
    int realtime_priority = 50;
    int cpu_affinity = -1; // -1 = no affinity
    
    // Data settings
    std::vector<ModbusAddress> addresses;
    bool enable_data_validation = true;
    bool enable_timestamp_correction = false;
    
    // Error handling
    bool enable_error_recovery = true;
    uint32_t max_consecutive_errors = 10;
    std::chrono::milliseconds error_backoff_time{1000};
    
    // Monitoring
    bool enable_statistics = true;
    std::chrono::milliseconds statistics_interval{1000};
    
    // ConfigurationBase interface
    ipb::common::Result<> validate() const override;
    std::string to_string() const override;
    ipb::common::Result<> from_string(std::string_view config) override;
    std::unique_ptr<ipb::common::ConfigurationBase> clone() const override;
    
    // Preset configurations
    static ModbusAdapterConfig create_high_performance();
    static ModbusAdapterConfig create_low_latency();
    static ModbusAdapterConfig create_reliable();
    static ModbusAdapterConfig create_minimal();
};

/**
 * @brief High-performance Modbus protocol adapter
 * 
 * Features:
 * - TCP and RTU support
 * - Asynchronous polling with configurable intervals
 * - Batch reading for optimal performance
 * - Real-time thread priority and CPU affinity
 * - Automatic error recovery and reconnection
 * - Zero-copy data handling where possible
 * - Lock-free statistics collection
 */
class ModbusAdapter : public ipb::common::IProtocolSourceBase {
public:
    static constexpr uint16_t PROTOCOL_ID = 1;
    static constexpr std::string_view PROTOCOL_NAME = "Modbus";
    static constexpr std::string_view COMPONENT_NAME = "ModbusAdapter";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";
    
    ModbusAdapter();
    ~ModbusAdapter() override;
    
    // Disable copy/move for thread safety
    ModbusAdapter(const ModbusAdapter&) = delete;
    ModbusAdapter& operator=(const ModbusAdapter&) = delete;
    ModbusAdapter(ModbusAdapter&&) = delete;
    ModbusAdapter& operator=(ModbusAdapter&&) = delete;
    
    // IProtocolSourceBase interface
    ipb::common::Result<ipb::common::DataSet> read() override;
    ipb::common::Result<ipb::common::DataSet> read_async() override;
    
    ipb::common::Result<> subscribe(DataCallback data_cb, ErrorCallback error_cb) override;
    ipb::common::Result<> unsubscribe() override;
    
    ipb::common::Result<> add_address(std::string_view address) override;
    ipb::common::Result<> remove_address(std::string_view address) override;
    std::vector<std::string> get_addresses() const override;
    
    ipb::common::Result<> connect() override;
    ipb::common::Result<> disconnect() override;
    bool is_connected() const noexcept override;
    
    uint16_t protocol_id() const noexcept override { return PROTOCOL_ID; }
    std::string_view protocol_name() const noexcept override { return PROTOCOL_NAME; }
    
    // IIPBComponent interface
    ipb::common::Result<> start() override;
    ipb::common::Result<> stop() override;
    bool is_running() const noexcept override;
    
    ipb::common::Result<> configure(const ipb::common::ConfigurationBase& config) override;
    std::unique_ptr<ipb::common::ConfigurationBase> get_configuration() const override;
    
    ipb::common::Statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;
    
    bool is_healthy() const noexcept override;
    std::string get_health_status() const override;
    
    std::string_view component_name() const noexcept override { return COMPONENT_NAME; }
    std::string_view component_version() const noexcept override { return COMPONENT_VERSION; }
    
    // Modbus-specific methods
    ipb::common::Result<> write_coil(uint8_t slave_id, uint16_t address, bool value);
    ipb::common::Result<> write_register(uint8_t slave_id, uint16_t address, uint16_t value);
    ipb::common::Result<> write_registers(uint8_t slave_id, uint16_t address, 
                                         std::span<const uint16_t> values);
    
    ipb::common::Result<std::vector<bool>> read_coils(uint8_t slave_id, uint16_t address, uint16_t count);
    ipb::common::Result<std::vector<bool>> read_discrete_inputs(uint8_t slave_id, uint16_t address, uint16_t count);
    ipb::common::Result<std::vector<uint16_t>> read_input_registers(uint8_t slave_id, uint16_t address, uint16_t count);
    ipb::common::Result<std::vector<uint16_t>> read_holding_registers(uint8_t slave_id, uint16_t address, uint16_t count);
    
    // Diagnostic methods
    ipb::common::Result<uint16_t> get_slave_id();
    ipb::common::Result<std::vector<uint8_t>> get_comm_event_counter();
    ipb::common::Result<std::vector<uint8_t>> get_comm_event_log();
    
private:
    // Configuration
    std::unique_ptr<ModbusAdapterConfig> config_;
    
    // Modbus context
    modbus_t* modbus_ctx_ = nullptr;
    
    // State management
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> is_subscribed_{false};
    
    // Threading
    std::unique_ptr<std::thread> polling_thread_;
    std::unique_ptr<std::thread> statistics_thread_;
    mutable std::mutex state_mutex_;
    std::condition_variable stop_condition_;
    
    // Callbacks
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    
    // Address management
    std::vector<ModbusAddress> addresses_;
    mutable std::shared_mutex addresses_mutex_;
    
    // Statistics (lock-free)
    mutable std::atomic<uint64_t> total_reads_{0};
    mutable std::atomic<uint64_t> successful_reads_{0};
    mutable std::atomic<uint64_t> failed_reads_{0};
    mutable std::atomic<uint64_t> total_bytes_{0};
    mutable std::atomic<uint64_t> connection_attempts_{0};
    mutable std::atomic<uint64_t> successful_connections_{0};
    mutable std::atomic<uint64_t> failed_connections_{0};
    
    // Error tracking
    std::atomic<uint32_t> consecutive_errors_{0};
    std::atomic<ipb::common::Timestamp> last_error_time_;
    std::atomic<ipb::common::Timestamp> last_successful_read_;
    
    // Performance tracking
    mutable std::atomic<int64_t> min_read_time_ns_{INT64_MAX};
    mutable std::atomic<int64_t> max_read_time_ns_{0};
    mutable std::atomic<int64_t> total_read_time_ns_{0};
    
    // Internal methods
    ipb::common::Result<> initialize_modbus();
    void cleanup_modbus();
    
    ipb::common::Result<> setup_connection();
    ipb::common::Result<> setup_realtime_settings();
    
    void polling_loop();
    void statistics_loop();
    
    ipb::common::Result<ipb::common::DataSet> read_addresses();
    ipb::common::Result<ipb::common::DataPoint> read_single_address(const ModbusAddress& addr);
    
    ipb::common::Value convert_raw_data(const std::vector<uint16_t>& raw_data, 
                                       DataType data_type, uint16_t count);
    
    void handle_error(const std::string& error_message, 
                     ipb::common::Result<>::ErrorCode error_code);
    void update_statistics(bool success, std::chrono::nanoseconds duration, size_t bytes = 0);
    
    bool should_retry_on_error() const;
    void perform_error_recovery();
    
    // Batch optimization
    struct BatchGroup {
        uint8_t slave_id;
        RegisterType register_type;
        uint16_t start_address;
        uint16_t end_address;
        std::vector<size_t> address_indices;
    };
    
    std::vector<BatchGroup> optimize_batch_reads() const;
    ipb::common::Result<ipb::common::DataSet> read_batch_group(const BatchGroup& group);
};

/**
 * @brief Factory for creating Modbus adapters
 */
class ModbusAdapterFactory {
public:
    static std::unique_ptr<ModbusAdapter> create(const ModbusAdapterConfig& config);
    static std::unique_ptr<ModbusAdapter> create_tcp(const std::string& host, uint16_t port);
    static std::unique_ptr<ModbusAdapter> create_rtu(const std::string& device, 
                                                    int baud_rate = 9600,
                                                    char parity = 'N',
                                                    int data_bits = 8,
                                                    int stop_bits = 1);
    
    // Preset factories
    static std::unique_ptr<ModbusAdapter> create_high_performance_tcp(const std::string& host, uint16_t port);
    static std::unique_ptr<ModbusAdapter> create_low_latency_tcp(const std::string& host, uint16_t port);
    static std::unique_ptr<ModbusAdapter> create_reliable_tcp(const std::string& host, uint16_t port);
};

} // namespace ipb::adapter::modbus

// Hash specialization for ModbusAddress
namespace std {
    template<>
    struct hash<ipb::adapter::modbus::ModbusAddress> {
        size_t operator()(const ipb::adapter::modbus::ModbusAddress& addr) const noexcept {
            return addr.hash();
        }
    };
}

