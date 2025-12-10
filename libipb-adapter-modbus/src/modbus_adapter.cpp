#include "ipb/adapter/modbus/modbus_adapter.hpp"
#include <modbus/modbus.h>
#include <json/json.h>
#include <thread>
#include <chrono>
#include <iostream>

namespace ipb::adapter::modbus {

ModbusAdapter::ModbusAdapter(const ModbusAdapterConfig& config)
    : config_(config) {
}

ModbusAdapter::~ModbusAdapter() {
    if (running_.load()) {
        stop();
    }
    shutdown();
}

common::Result<void> ModbusAdapter::initialize(const std::string& config_path) {
    try {
        // Initialize libmodbus context
        if (config_.connection_type == ModbusConnectionType::TCP) {
            modbus_ctx_ = modbus_new_tcp(config_.host.c_str(), config_.port);
        } else {
            modbus_ctx_ = modbus_new_rtu(config_.device.c_str(), config_.baud_rate, 
                                        config_.parity, config_.data_bits, config_.stop_bits);
        }
        
        if (!modbus_ctx_) {
            return common::Result<void>::failure("Failed to create Modbus context");
        }
        
        // Set slave ID
        if (modbus_set_slave(modbus_ctx_, config_.slave_id) == -1) {
            modbus_free(modbus_ctx_);
            modbus_ctx_ = nullptr;
            return common::Result<void>::failure("Failed to set Modbus slave ID");
        }
        
        // Set timeouts
        modbus_set_response_timeout(modbus_ctx_, 
                                   config_.response_timeout.count() / 1000, 
                                   (config_.response_timeout.count() % 1000) * 1000);
        
        // Set debug mode if enabled
        if (config_.enable_debug) {
            modbus_set_debug(modbus_ctx_, TRUE);
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to initialize Modbus adapter: " + std::string(e.what())
        );
    }
}

common::Result<void> ModbusAdapter::start() {
    if (running_.load()) {
        return common::Result<void>::failure("Modbus adapter is already running");
    }
    
    try {
        // Connect to Modbus device
        if (modbus_connect(modbus_ctx_) == -1) {
            return common::Result<void>::failure(
                "Failed to connect to Modbus device: " + std::string(modbus_strerror(errno))
            );
        }
        
        running_.store(true);
        shutdown_requested_.store(false);
        
        // Start polling thread
        polling_thread_ = std::thread(&ModbusAdapter::polling_loop, this);
        
        // Start statistics thread if enabled
        if (config_.enable_statistics) {
            statistics_thread_ = std::thread(&ModbusAdapter::statistics_loop, this);
        }
        
        // Reset statistics
        statistics_.reset();
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        running_.store(false);
        return common::Result<void>::failure(
            "Failed to start Modbus adapter: " + std::string(e.what())
        );
    }
}

common::Result<void> ModbusAdapter::stop() {
    if (!running_.load()) {
        return common::Result<void>::success();
    }
    
    try {
        running_.store(false);
        
        // Wait for polling thread to finish
        if (polling_thread_.joinable()) {
            polling_thread_.join();
        }
        
        // Stop statistics thread
        if (statistics_thread_.joinable()) {
            statistics_thread_.join();
        }
        
        // Disconnect from Modbus device
        if (modbus_ctx_) {
            modbus_close(modbus_ctx_);
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to stop Modbus adapter: " + std::string(e.what())
        );
    }
}

common::Result<void> ModbusAdapter::shutdown() {
    shutdown_requested_.store(true);
    
    auto stop_result = stop();
    if (!stop_result.is_success()) {
        return stop_result;
    }
    
    try {
        // Free Modbus context
        if (modbus_ctx_) {
            modbus_free(modbus_ctx_);
            modbus_ctx_ = nullptr;
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to shutdown Modbus adapter: " + std::string(e.what())
        );
    }
}

bool ModbusAdapter::is_connected() const {
    return running_.load() && modbus_ctx_ != nullptr;
}

bool ModbusAdapter::is_healthy() const {
    if (!running_.load() || !modbus_ctx_) {
        return false;
    }
    
    // Check if error rate is acceptable
    auto total_requests = statistics_.successful_reads.load() + statistics_.failed_reads.load();
    if (total_requests > 0) {
        auto error_rate = static_cast<double>(statistics_.failed_reads.load()) / total_requests;
        return error_rate < 0.1;  // Less than 10% error rate
    }
    
    return true;
}

common::ProtocolMetrics ModbusAdapter::get_metrics() const {
    common::ProtocolMetrics metrics;
    metrics.protocol_id = "modbus";
    metrics.messages_sent = statistics_.successful_reads.load();
    metrics.messages_failed = statistics_.failed_reads.load();
    metrics.bytes_sent = statistics_.bytes_read.load();
    metrics.is_connected = is_connected();
    metrics.is_healthy = is_healthy();
    metrics.avg_processing_time = statistics_.get_average_read_time();
    
    return metrics;
}

std::string ModbusAdapter::get_protocol_info() const {
    Json::Value info;
    info["protocol"] = "modbus";
    info["connection_type"] = (config_.connection_type == ModbusConnectionType::TCP) ? "tcp" : "rtu";
    info["slave_id"] = config_.slave_id;
    
    if (config_.connection_type == ModbusConnectionType::TCP) {
        info["host"] = config_.host;
        info["port"] = config_.port;
    } else {
        info["device"] = config_.device;
        info["baud_rate"] = config_.baud_rate;
    }
    
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, info);
}

void ModbusAdapter::polling_loop() {
    while (running_.load()) {
        auto cycle_start = std::chrono::high_resolution_clock::now();
        
        // Poll all configured registers
        for (const auto& register_config : config_.registers) {
            if (!running_.load()) break;
            
            auto read_result = read_register(register_config);
            if (read_result.is_success()) {
                auto data_point = read_result.get_value();
                
                // Send data point to router if callback is set
                if (data_callback_) {
                    data_callback_(data_point);
                }
                
                statistics_.successful_reads.fetch_add(1);
            } else {
                statistics_.failed_reads.fetch_add(1);
                
                if (config_.enable_debug) {
                    std::cerr << "Failed to read register " << register_config.address 
                             << ": " << read_result.get_error() << std::endl;
                }
            }
        }
        
        // Calculate cycle time and sleep if necessary
        auto cycle_end = std::chrono::high_resolution_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            cycle_end - cycle_start
        );
        
        if (cycle_duration < config_.polling_interval) {
            std::this_thread::sleep_for(config_.polling_interval - cycle_duration);
        }
    }
}

void ModbusAdapter::statistics_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(config_.statistics_interval);
        
        if (running_.load()) {
            print_statistics();
        }
    }
}

common::Result<common::DataPoint> ModbusAdapter::read_register(const ModbusRegisterConfig& register_config) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        std::vector<uint16_t> data(register_config.count);
        int result = -1;
        
        switch (register_config.type) {
            case ModbusRegisterType::COIL:
                {
                    std::vector<uint8_t> coil_data(register_config.count);
                    result = modbus_read_bits(modbus_ctx_, register_config.address, 
                                            register_config.count, coil_data.data());
                    if (result != -1) {
                        for (size_t i = 0; i < coil_data.size(); ++i) {
                            data[i] = coil_data[i];
                        }
                    }
                }
                break;
                
            case ModbusRegisterType::DISCRETE_INPUT:
                {
                    std::vector<uint8_t> input_data(register_config.count);
                    result = modbus_read_input_bits(modbus_ctx_, register_config.address, 
                                                  register_config.count, input_data.data());
                    if (result != -1) {
                        for (size_t i = 0; i < input_data.size(); ++i) {
                            data[i] = input_data[i];
                        }
                    }
                }
                break;
                
            case ModbusRegisterType::HOLDING_REGISTER:
                result = modbus_read_registers(modbus_ctx_, register_config.address, 
                                             register_config.count, data.data());
                break;
                
            case ModbusRegisterType::INPUT_REGISTER:
                result = modbus_read_input_registers(modbus_ctx_, register_config.address, 
                                                   register_config.count, data.data());
                break;
        }
        
        if (result == -1) {
            return common::Result<common::DataPoint>::failure(
                "Modbus read failed: " + std::string(modbus_strerror(errno))
            );
        }
        
        // Create data point
        common::DataPoint data_point;
        data_point.set_protocol_id("modbus");
        data_point.set_address(register_config.name);
        data_point.set_timestamp(std::chrono::system_clock::now());
        data_point.set_quality(common::DataQuality::GOOD);
        
        // Convert data based on data type
        if (register_config.count == 1) {
            switch (register_config.data_type) {
                case ModbusDataType::UINT16:
                    data_point.set_value(static_cast<uint16_t>(data[0]));
                    break;
                case ModbusDataType::INT16:
                    data_point.set_value(static_cast<int16_t>(data[0]));
                    break;
                case ModbusDataType::BOOL:
                    data_point.set_value(data[0] != 0);
                    break;
                case ModbusDataType::FLOAT32:
                    if (register_config.count >= 2) {
                        uint32_t combined = (static_cast<uint32_t>(data[0]) << 16) | data[1];
                        float float_value;
                        memcpy(&float_value, &combined, sizeof(float));
                        data_point.set_value(float_value);
                    }
                    break;
            }
        } else {
            // For multiple registers, store as array
            std::vector<uint16_t> values(data.begin(), data.begin() + register_config.count);
            // Note: DataPoint would need to support array types for this to work properly
            data_point.set_value(static_cast<uint16_t>(data[0]));  // Simplified for now
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto read_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time
        );
        statistics_.update_read_time(read_time);
        statistics_.bytes_read.fetch_add(register_config.count * 2);  // 2 bytes per register
        
        return common::Result<common::DataPoint>::success(std::move(data_point));
        
    } catch (const std::exception& e) {
        return common::Result<common::DataPoint>::failure(
            "Exception during Modbus read: " + std::string(e.what())
        );
    }
}

void ModbusAdapter::print_statistics() const {
    if (!config_.enable_statistics) {
        return;
    }
    
    auto stats = get_statistics();
    
    std::cout << "Modbus Adapter Statistics: "
              << "successful_reads=" << stats.successful_reads.load()
              << ", failed_reads=" << stats.failed_reads.load()
              << ", bytes_read=" << stats.bytes_read.load()
              << ", avg_read_time=" << stats.get_average_read_time().count() << "ns"
              << std::endl;
}

// Factory implementations
std::unique_ptr<ModbusAdapter> ModbusAdapterFactory::create_tcp(
    const std::string& host, uint16_t port, uint8_t slave_id) {
    
    ModbusAdapterConfig config;
    config.connection_type = ModbusConnectionType::TCP;
    config.host = host;
    config.port = port;
    config.slave_id = slave_id;
    
    return std::make_unique<ModbusAdapter>(config);
}

std::unique_ptr<ModbusAdapter> ModbusAdapterFactory::create_rtu(
    const std::string& device, uint32_t baud_rate, uint8_t slave_id) {
    
    ModbusAdapterConfig config;
    config.connection_type = ModbusConnectionType::RTU;
    config.device = device;
    config.baud_rate = baud_rate;
    config.slave_id = slave_id;
    
    return std::make_unique<ModbusAdapter>(config);
}

std::unique_ptr<ModbusAdapter> ModbusAdapterFactory::create(const ModbusAdapterConfig& config) {
    return std::make_unique<ModbusAdapter>(config);
}

} // namespace ipb::adapter::modbus

