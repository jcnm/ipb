#include "ipb/scoop/modbus/modbus_scoop.hpp"

#include <ipb/common/debug.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <chrono>
#include <iostream>
#include <thread>

#include <json/json.h>
#include <modbus/modbus.h>

namespace ipb::scoop::modbus {

using namespace common::debug;

namespace {
constexpr const char* LOG_CAT = category::PROTOCOL;
}  // namespace

ModbusScoop::ModbusScoop(const ModbusScoopConfig& config) : config_(config) {
    IPB_LOG_DEBUG(LOG_CAT, "ModbusScoop created");
}

ModbusScoop::~ModbusScoop() {
    IPB_LOG_TRACE(LOG_CAT, "ModbusScoop destructor");
    if (running_.load()) {
        stop();
    }
    shutdown();
}

common::Result<void> ModbusScoop::initialize(const std::string& config_path) {
    IPB_SPAN_CAT("ModbusScoop::initialize", LOG_CAT);
    IPB_LOG_INFO(LOG_CAT, "Initializing ModbusScoop...");

    try {
        // Initialize libmodbus context
        if (config_.connection_type == ModbusConnectionType::TCP) {
            IPB_LOG_DEBUG(LOG_CAT, "Creating TCP context: " << config_.host << ":" << config_.port);
            modbus_ctx_ = modbus_new_tcp(config_.host.c_str(), config_.port);
        } else {
            IPB_LOG_DEBUG(LOG_CAT,
                          "Creating RTU context: " << config_.device << " @ " << config_.baud_rate);
            modbus_ctx_ = modbus_new_rtu(config_.device.c_str(), config_.baud_rate, config_.parity,
                                         config_.data_bits, config_.stop_bits);
        }

        if (IPB_UNLIKELY(!modbus_ctx_)) {
            IPB_LOG_ERROR(LOG_CAT, "Failed to create Modbus context");
            return common::Result<void>::failure("Failed to create Modbus context");
        }

        // Set slave ID
        if (IPB_UNLIKELY(modbus_set_slave(modbus_ctx_, config_.slave_id) == -1)) {
            IPB_LOG_ERROR(LOG_CAT, "Failed to set Modbus slave ID: " << config_.slave_id);
            modbus_free(modbus_ctx_);
            modbus_ctx_ = nullptr;
            return common::Result<void>::failure("Failed to set Modbus slave ID");
        }

        // Set timeouts
        modbus_set_response_timeout(modbus_ctx_, config_.response_timeout.count() / 1000,
                                    (config_.response_timeout.count() % 1000) * 1000);

        // Set debug mode if enabled
        if (config_.enable_debug) {
            modbus_set_debug(modbus_ctx_, TRUE);
        }

        IPB_LOG_INFO(LOG_CAT, "ModbusScoop initialized successfully");
        return common::Result<void>::success();

    } catch (const std::exception& e) {
        IPB_LOG_ERROR(LOG_CAT, "Exception during initialization: " << e.what());
        return common::Result<void>::failure("Failed to initialize Modbus scoop: " +
                                             std::string(e.what()));
    }
}

common::Result<void> ModbusScoop::start() {
    IPB_SPAN_CAT("ModbusScoop::start", LOG_CAT);

    if (IPB_UNLIKELY(running_.load())) {
        IPB_LOG_WARN(LOG_CAT, "Modbus scoop is already running");
        return common::Result<void>::failure("Modbus scoop is already running");
    }

    IPB_LOG_INFO(LOG_CAT, "Starting ModbusScoop...");

    try {
        // Connect to Modbus device
        if (IPB_UNLIKELY(modbus_connect(modbus_ctx_) == -1)) {
            IPB_LOG_ERROR(LOG_CAT,
                          "Failed to connect to Modbus device: " << modbus_strerror(errno));
            return common::Result<void>::failure("Failed to connect to Modbus device: " +
                                                 std::string(modbus_strerror(errno)));
        }

        IPB_LOG_DEBUG(LOG_CAT, "Connected to Modbus device");

        running_.store(true);
        shutdown_requested_.store(false);

        // Start polling thread
        polling_thread_ = std::thread(&ModbusScoop::polling_loop, this);
        IPB_LOG_DEBUG(LOG_CAT, "Polling thread started");

        // Start statistics thread if enabled
        if (config_.enable_statistics) {
            statistics_thread_ = std::thread(&ModbusScoop::statistics_loop, this);
            IPB_LOG_DEBUG(LOG_CAT, "Statistics thread started");
        }

        // Reset statistics
        statistics_.reset();

        IPB_LOG_INFO(LOG_CAT, "ModbusScoop started successfully");
        return common::Result<void>::success();

    } catch (const std::exception& e) {
        IPB_LOG_ERROR(LOG_CAT, "Exception during start: " << e.what());
        running_.store(false);
        return common::Result<void>::failure("Failed to start Modbus scoop: " +
                                             std::string(e.what()));
    }
}

common::Result<void> ModbusScoop::stop() {
    IPB_SPAN_CAT("ModbusScoop::stop", LOG_CAT);

    if (!running_.load()) {
        IPB_LOG_DEBUG(LOG_CAT, "ModbusScoop already stopped");
        return common::Result<void>::success();
    }

    IPB_LOG_INFO(LOG_CAT, "Stopping ModbusScoop...");

    try {
        running_.store(false);

        // Wait for polling thread to finish
        if (polling_thread_.joinable()) {
            polling_thread_.join();
            IPB_LOG_DEBUG(LOG_CAT, "Polling thread stopped");
        }

        // Stop statistics thread
        if (statistics_thread_.joinable()) {
            statistics_thread_.join();
            IPB_LOG_DEBUG(LOG_CAT, "Statistics thread stopped");
        }

        // Disconnect from Modbus device
        if (modbus_ctx_) {
            modbus_close(modbus_ctx_);
            IPB_LOG_DEBUG(LOG_CAT, "Disconnected from Modbus device");
        }

        IPB_LOG_INFO(LOG_CAT, "ModbusScoop stopped successfully");
        return common::Result<void>::success();

    } catch (const std::exception& e) {
        IPB_LOG_ERROR(LOG_CAT, "Exception during stop: " << e.what());
        return common::Result<void>::failure("Failed to stop Modbus scoop: " +
                                             std::string(e.what()));
    }
}

common::Result<void> ModbusScoop::shutdown() {
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
        return common::Result<void>::failure("Failed to shutdown Modbus scoop: " +
                                             std::string(e.what()));
    }
}

bool ModbusScoop::is_connected() const {
    return running_.load() && modbus_ctx_ != nullptr;
}

bool ModbusScoop::is_healthy() const {
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

common::ProtocolMetrics ModbusScoop::get_metrics() const {
    common::ProtocolMetrics metrics;
    metrics.protocol_id         = "modbus";
    metrics.messages_sent       = statistics_.successful_reads.load();
    metrics.messages_failed     = statistics_.failed_reads.load();
    metrics.bytes_sent          = statistics_.bytes_read.load();
    metrics.is_connected        = is_connected();
    metrics.is_healthy          = is_healthy();
    metrics.avg_processing_time = statistics_.get_average_read_time();

    return metrics;
}

std::string ModbusScoop::get_protocol_info() const {
    Json::Value info;
    info["protocol"] = "modbus";
    info["connection_type"] =
        (config_.connection_type == ModbusConnectionType::TCP) ? "tcp" : "rtu";
    info["slave_id"] = config_.slave_id;

    if (config_.connection_type == ModbusConnectionType::TCP) {
        info["host"] = config_.host;
        info["port"] = config_.port;
    } else {
        info["device"]    = config_.device;
        info["baud_rate"] = config_.baud_rate;
    }

    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, info);
}

void ModbusScoop::polling_loop() {
    IPB_LOG_DEBUG(LOG_CAT, "Polling loop started");

    while (running_.load()) {
        auto cycle_start = std::chrono::high_resolution_clock::now();

        // Poll all configured registers
        for (const auto& register_config : config_.registers) {
            if (!running_.load())
                break;

            auto read_result = read_register(register_config);
            if (IPB_LIKELY(read_result.is_success())) {
                auto data_point = read_result.get_value();

                // Send data point to router if callback is set
                if (data_callback_) {
                    data_callback_(data_point);
                }

                statistics_.successful_reads.fetch_add(1);
            } else {
                statistics_.failed_reads.fetch_add(1);
                IPB_LOG_WARN(LOG_CAT, "Failed to read register " << register_config.address << ": "
                                                                 << read_result.get_error());
            }
        }

        // Calculate cycle time and sleep if necessary
        auto cycle_end = std::chrono::high_resolution_clock::now();
        auto cycle_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(cycle_end - cycle_start);

        if (cycle_duration < config_.polling_interval) {
            std::this_thread::sleep_for(config_.polling_interval - cycle_duration);
        }
    }

    IPB_LOG_DEBUG(LOG_CAT, "Polling loop stopped");
}

void ModbusScoop::statistics_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(config_.statistics_interval);

        if (running_.load()) {
            print_statistics();
        }
    }
}

common::Result<common::DataPoint> ModbusScoop::read_register(
    const ModbusRegisterConfig& register_config) {
    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        std::vector<uint16_t> data(register_config.count);
        int result = -1;

        switch (register_config.type) {
            case ModbusRegisterType::COIL: {
                std::vector<uint8_t> coil_data(register_config.count);
                result = modbus_read_bits(modbus_ctx_, register_config.address,
                                          register_config.count, coil_data.data());
                if (result != -1) {
                    for (size_t i = 0; i < coil_data.size(); ++i) {
                        data[i] = coil_data[i];
                    }
                }
            } break;

            case ModbusRegisterType::DISCRETE_INPUT: {
                std::vector<uint8_t> input_data(register_config.count);
                result = modbus_read_input_bits(modbus_ctx_, register_config.address,
                                                register_config.count, input_data.data());
                if (result != -1) {
                    for (size_t i = 0; i < input_data.size(); ++i) {
                        data[i] = input_data[i];
                    }
                }
            } break;

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
            return common::Result<common::DataPoint>::failure("Modbus read failed: " +
                                                              std::string(modbus_strerror(errno)));
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
        auto read_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        statistics_.update_read_time(read_time);
        statistics_.bytes_read.fetch_add(register_config.count * 2);  // 2 bytes per register

        return common::Result<common::DataPoint>::success(std::move(data_point));

    } catch (const std::exception& e) {
        return common::Result<common::DataPoint>::failure("Exception during Modbus read: " +
                                                          std::string(e.what()));
    }
}

void ModbusScoop::print_statistics() const {
    if (!config_.enable_statistics) {
        return;
    }

    auto stats = get_statistics();

    std::cout << "Modbus Adapter Statistics: " << "successful_reads="
              << stats.successful_reads.load() << ", failed_reads=" << stats.failed_reads.load()
              << ", bytes_read=" << stats.bytes_read.load()
              << ", avg_read_time=" << stats.get_average_read_time().count() << "ns" << std::endl;
}

// Factory implementations
std::unique_ptr<ModbusScoop> ModbusScoopFactory::create_tcp(const std::string& host, uint16_t port,
                                                            uint8_t slave_id) {
    ModbusScoopConfig config;
    config.connection_type = ModbusConnectionType::TCP;
    config.host            = host;
    config.port            = port;
    config.slave_id        = slave_id;

    return std::make_unique<ModbusScoop>(config);
}

std::unique_ptr<ModbusScoop> ModbusScoopFactory::create_rtu(const std::string& device,
                                                            uint32_t baud_rate, uint8_t slave_id) {
    ModbusScoopConfig config;
    config.connection_type = ModbusConnectionType::RTU;
    config.device          = device;
    config.baud_rate       = baud_rate;
    config.slave_id        = slave_id;

    return std::make_unique<ModbusScoop>(config);
}

std::unique_ptr<ModbusScoop> ModbusScoopFactory::create(const ModbusScoopConfig& config) {
    return std::make_unique<ModbusScoop>(config);
}

}  // namespace ipb::scoop::modbus
