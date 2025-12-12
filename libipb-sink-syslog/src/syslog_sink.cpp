#include "ipb/sink/syslog/syslog_sink.hpp"
#include <json/json.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>  // For ::syslog() function
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <regex>

namespace ipb::sink::syslog {

SyslogSink::SyslogSink(const SyslogSinkConfig& config) 
    : config_(config) {
}

SyslogSink::~SyslogSink() {
    if (running_.load()) {
        stop();
    }
    shutdown();
}

common::Result<void> SyslogSink::initialize(const std::string& config_path) {
    try {
        // Open local syslog
        int options = LOG_NDELAY;
        if (config_.include_pid) options |= LOG_PID;
        if (config_.log_to_stderr) options |= LOG_CONS;
        if (config_.log_perror) options |= LOG_PERROR;
        
        openlog(config_.ident.c_str(), options, static_cast<int>(config_.facility));
        
        // Initialize fallback file if enabled
        if (config_.fallback_config.enable_file_fallback) {
            fallback_file_stream_ = std::make_unique<std::ofstream>(
                config_.fallback_config.fallback_file_path,
                std::ios::out | std::ios::app
            );
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to initialize syslog sink: " + std::string(e.what())
        );
    }
}

common::Result<void> SyslogSink::start() {
    if (running_.load()) {
        return common::Result<void>::failure("Syslog sink is already running");
    }
    
    try {
        running_.store(true);
        shutdown_requested_.store(false);
        fallback_active_.store(false);
        consecutive_failures_.store(0);
        
        // Establish remote connection if enabled
        if (config_.enable_remote_syslog) {
            auto result = establish_remote_connection();
            if (!result.is_success()) {
                syslog(LOG_WARNING, "Failed to establish remote syslog connection: %s", 
                       result.get_error().c_str());
                activate_fallback();
            }
        }
        
        // Start worker thread for async processing
        if (config_.enable_async_logging) {
            worker_thread_ = std::thread(&SyslogSink::worker_loop, this);
        }
        
        // Start statistics thread if enabled
        if (config_.enable_statistics) {
            statistics_thread_ = std::thread(&SyslogSink::statistics_loop, this);
        }
        
        // Start recovery thread for fallback recovery
        recovery_thread_ = std::thread(&SyslogSink::recovery_loop, this);
        
        statistics_.reset();
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        running_.store(false);
        return common::Result<void>::failure(
            "Failed to start syslog sink: " + std::string(e.what())
        );
    }
}

common::Result<void> SyslogSink::stop() {
    if (!running_.load()) {
        return common::Result<void>::success();
    }
    
    try {
        running_.store(false);
        
        // Wake up worker thread
        if (config_.enable_async_logging) {
            queue_condition_.notify_all();
            
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
        }
        
        // Stop other threads
        if (statistics_thread_.joinable()) {
            statistics_thread_.join();
        }
        
        if (recovery_thread_.joinable()) {
            recovery_thread_.join();
        }
        
        // Close remote connection
        close_remote_connection();
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to stop syslog sink: " + std::string(e.what())
        );
    }
}

common::Result<void> SyslogSink::shutdown() {
    shutdown_requested_.store(true);
    
    auto stop_result = stop();
    if (!stop_result.is_success()) {
        return stop_result;
    }
    
    try {
        // Close local syslog
        closelog();
        
        // Close fallback file
        if (fallback_file_stream_ && fallback_file_stream_->is_open()) {
            fallback_file_stream_->close();
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to shutdown syslog sink: " + std::string(e.what())
        );
    }
}

common::Result<void> SyslogSink::send_data_point(const common::DataPoint& data_point) {
    if (!running_.load()) {
        return common::Result<void>::failure("Syslog sink is not running");
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // Apply filtering
        if (should_filter_message(data_point)) {
            statistics_.messages_filtered.fetch_add(1);
            return common::Result<void>::success();
        }
        
        if (config_.enable_async_logging) {
            // Add to queue for async processing
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                
                if (message_queue_.size() >= config_.queue_size) {
                    statistics_.messages_dropped.fetch_add(1);
                    return common::Result<void>::failure("Message queue is full");
                }
                
                message_queue_.push(data_point);
            }
            
            queue_condition_.notify_one();
        } else {
            // Process synchronously
            auto priority = determine_priority(data_point);
            std::string formatted_message = format_message(data_point, priority);
            
            common::Result<void> result;
            if (config_.enable_remote_syslog && !fallback_active_.load()) {
                result = send_to_remote_syslog(formatted_message);
                if (!result.is_success()) {
                    handle_send_failure();
                    result = send_to_fallback(formatted_message);
                }
            } else {
                result = send_to_local_syslog(formatted_message, priority);
            }
            
            if (!result.is_success()) {
                statistics_.messages_failed.fetch_add(1);
                return result;
            }
        }
        
        statistics_.messages_processed.fetch_add(1);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time
        );
        statistics_.update_processing_time(processing_time);
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        statistics_.messages_failed.fetch_add(1);
        return common::Result<void>::failure(
            "Failed to send data point: " + std::string(e.what())
        );
    }
}

common::Result<void> SyslogSink::send_data_set(const common::DataSet& data_set) {
    if (!running_.load()) {
        return common::Result<void>::failure("Syslog sink is not running");
    }
    
    try {
        // Process each data point in the set
        for (const auto& data_point : data_set.get_data_points()) {
            auto result = send_data_point(data_point);
            if (!result.is_success()) {
                return result;
            }
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to send data set: " + std::string(e.what())
        );
    }
}

bool SyslogSink::is_connected() const {
    if (!running_.load()) {
        return false;
    }
    
    if (config_.enable_remote_syslog) {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        return remote_socket_ != -1 && !fallback_active_.load();
    }
    
    return true;  // Local syslog is always "connected"
}

bool SyslogSink::is_healthy() const {
    if (!running_.load()) {
        return false;
    }
    
    // Check if we're in fallback mode due to too many failures
    if (fallback_active_.load()) {
        return false;
    }
    
    // Check if failure rate is too high
    auto failure_rate = static_cast<double>(statistics_.messages_failed.load()) / 
                       std::max(1UL, statistics_.messages_processed.load());
    
    return failure_rate < 0.1;  // Less than 10% failure rate
}

common::SinkMetrics SyslogSink::get_metrics() const {
    common::SinkMetrics metrics;
    metrics.sink_id = "syslog_sink";
    metrics.messages_sent = statistics_.messages_sent.load();
    metrics.messages_failed = statistics_.messages_failed.load();
    metrics.bytes_sent = statistics_.bytes_sent.load();
    metrics.is_connected = is_connected();
    metrics.is_healthy = is_healthy();
    metrics.avg_processing_time = statistics_.get_average_processing_time();
    
    return metrics;
}

std::string SyslogSink::get_sink_info() const {
    Json::Value info;
    info["type"] = "syslog";
    info["facility"] = static_cast<int>(config_.facility);
    info["format"] = static_cast<int>(config_.format);
    info["remote_enabled"] = config_.enable_remote_syslog;
    info["async_enabled"] = config_.enable_async_logging;
    info["fallback_active"] = fallback_active_.load();
    
    if (config_.enable_remote_syslog) {
        info["remote_host"] = config_.remote_host;
        info["remote_port"] = config_.remote_port;
        info["transport"] = static_cast<int>(config_.transport);
    }
    
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, info);
}

void SyslogSink::worker_loop() {
    std::vector<common::DataPoint> batch;
    batch.reserve(config_.batch_size);
    
    while (running_.load() || !message_queue_.empty()) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Wait for messages or timeout
            queue_condition_.wait_for(lock, config_.flush_interval, [this] {
                return !message_queue_.empty() || !running_.load();
            });
            
            // Collect batch of messages
            while (!message_queue_.empty() && batch.size() < config_.batch_size) {
                batch.push_back(std::move(message_queue_.front()));
                message_queue_.pop();
            }
        }
        
        // Process batch
        if (!batch.empty()) {
            process_message_batch(batch);
            batch.clear();
        }
    }
}

void SyslogSink::statistics_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(config_.statistics_interval);
        
        if (running_.load()) {
            print_statistics();
        }
    }
}

void SyslogSink::recovery_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(config_.fallback_config.recovery_check_interval);
        
        if (running_.load() && fallback_active_.load()) {
            auto result = recover_from_fallback();
            if (result.is_success()) {
                syslog(LOG_INFO, "Successfully recovered from fallback mode");
            }
        }
    }
}

bool SyslogSink::should_filter_message(const common::DataPoint& data_point) const {
    if (!config_.enable_filtering) {
        return false;
    }
    
    // Check address filters
    if (!config_.address_filters.empty()) {
        bool address_match = false;
        for (const auto& pattern : config_.address_filters) {
            try {
                std::regex regex(pattern);
                if (std::regex_match(data_point.get_address(), regex)) {
                    address_match = true;
                    break;
                }
            } catch (const std::regex_error&) {
                // Invalid regex, skip
                continue;
            }
        }
        if (!address_match) {
            return true;  // Filter out
        }
    }
    
    // Check protocol ID filters
    if (!config_.protocol_id_filters.empty()) {
        auto protocol_id = data_point.get_protocol_id();
        if (std::find(config_.protocol_id_filters.begin(), 
                     config_.protocol_id_filters.end(), 
                     protocol_id) == config_.protocol_id_filters.end()) {
            return true;  // Filter out
        }
    }
    
    // Check quality filters
    if (!config_.quality_filters.empty()) {
        auto quality = data_point.get_quality();
        if (std::find(config_.quality_filters.begin(), 
                     config_.quality_filters.end(), 
                     quality) == config_.quality_filters.end()) {
            return true;  // Filter out
        }
    }
    
    return false;  // Don't filter
}

SyslogPriority SyslogSink::determine_priority(const common::DataPoint& data_point) const {
    const auto& mapping = config_.priority_mapping;
    
    // Try custom callback first
    if (mapping.custom_priority_callback) {
        return mapping.custom_priority_callback(data_point);
    }
    
    // Check address-based mapping
    auto addr_it = mapping.address_priority_map.find(data_point.get_address());
    if (addr_it != mapping.address_priority_map.end()) {
        return addr_it->second;
    }
    
    // Check protocol-based mapping
    auto proto_it = mapping.protocol_priority_map.find(data_point.get_protocol_id());
    if (proto_it != mapping.protocol_priority_map.end()) {
        return proto_it->second;
    }
    
    // Check quality-based mapping
    auto quality_it = mapping.quality_priority_map.find(data_point.get_quality());
    if (quality_it != mapping.quality_priority_map.end()) {
        return quality_it->second;
    }
    
    return mapping.default_priority;
}

std::string SyslogSink::format_message(const common::DataPoint& data_point, SyslogPriority priority) const {
    switch (config_.format) {
        case SyslogFormat::RFC3164:
            return format_rfc3164(data_point, priority);
        case SyslogFormat::RFC5424:
            return format_rfc5424(data_point, priority);
        case SyslogFormat::CEF:
            return format_cef(data_point, priority);
        case SyslogFormat::LEEF:
            return format_leef(data_point, priority);
        case SyslogFormat::JSON:
            return format_json(data_point, priority);
        case SyslogFormat::PLAIN:
            return format_plain(data_point, priority);
        default:
            return format_rfc5424(data_point, priority);
    }
}

std::string SyslogSink::format_rfc5424(const common::DataPoint& data_point, SyslogPriority priority) const {
    std::ostringstream oss;
    
    // Priority value
    int pri = static_cast<int>(config_.facility) + static_cast<int>(priority);
    oss << "<" << pri << ">";
    
    // Version
    oss << "1 ";
    
    // Timestamp
    oss << format_timestamp_rfc5424() << " ";
    
    // Hostname
    oss << get_hostname() << " ";
    
    // App name
    oss << config_.app_name << " ";
    
    // Process ID
    oss << get_process_id() << " ";
    
    // Message ID
    oss << config_.msg_id << " ";
    
    // Structured data (none for now)
    oss << "- ";
    
    // Message
    oss << "Protocol=" << data_point.get_protocol_id() 
        << " Address=" << data_point.get_address()
        << " Quality=" << static_cast<int>(data_point.get_quality());
    
    if (data_point.get_value().has_value()) {
        oss << " Value=";
        // Format value based on type
        const auto& val = data_point.value();  // Direct access to avoid dangling
        using Type = common::Value::Type;
        switch (val.type()) {
            case Type::STRING:
                oss << "\"" << val.as_string_view() << "\"";
                break;
            case Type::BOOL:
                oss << (val.get<bool>() ? "true" : "false");
                break;
            case Type::INT8:
                oss << static_cast<int>(val.get<int8_t>());
                break;
            case Type::INT16:
                oss << val.get<int16_t>();
                break;
            case Type::INT32:
                oss << val.get<int32_t>();
                break;
            case Type::INT64:
                oss << val.get<int64_t>();
                break;
            case Type::UINT8:
                oss << static_cast<unsigned>(val.get<uint8_t>());
                break;
            case Type::UINT16:
                oss << val.get<uint16_t>();
                break;
            case Type::UINT32:
                oss << val.get<uint32_t>();
                break;
            case Type::UINT64:
                oss << val.get<uint64_t>();
                break;
            case Type::FLOAT32:
                oss << val.get<float>();
                break;
            case Type::FLOAT64:
                oss << val.get<double>();
                break;
            default:
                break;
        }
    }
    
    return oss.str();
}

std::string SyslogSink::format_json(const common::DataPoint& data_point, SyslogPriority priority) const {
    Json::Value json;
    
    json["timestamp"] = format_timestamp_rfc5424();
    json["hostname"] = get_hostname();
    json["app_name"] = config_.app_name;
    json["process_id"] = get_process_id();
    json["facility"] = static_cast<int>(config_.facility);
    json["priority"] = static_cast<int>(priority);
    json["protocol_id"] = data_point.get_protocol_id();
    json["address"] = std::string(data_point.get_address());
    json["quality"] = static_cast<int>(data_point.get_quality());

    if (data_point.get_value().has_value()) {
        const auto& val = data_point.value();  // Direct access to avoid dangling
        using Type = common::Value::Type;
        switch (val.type()) {
            case Type::STRING:
                json["value"] = std::string(val.as_string_view());
                break;
            case Type::BOOL:
                json["value"] = val.get<bool>();
                break;
            case Type::INT8:
                json["value"] = val.get<int8_t>();
                break;
            case Type::INT16:
                json["value"] = val.get<int16_t>();
                break;
            case Type::INT32:
                json["value"] = val.get<int32_t>();
                break;
            case Type::INT64:
                json["value"] = static_cast<Json::Int64>(val.get<int64_t>());
                break;
            case Type::UINT8:
                json["value"] = val.get<uint8_t>();
                break;
            case Type::UINT16:
                json["value"] = val.get<uint16_t>();
                break;
            case Type::UINT32:
                json["value"] = val.get<uint32_t>();
                break;
            case Type::UINT64:
                json["value"] = static_cast<Json::UInt64>(val.get<uint64_t>());
                break;
            case Type::FLOAT32:
                json["value"] = val.get<float>();
                break;
            case Type::FLOAT64:
                json["value"] = val.get<double>();
                break;
            default:
                break;
        }
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, json);
}

std::string SyslogSink::get_hostname() const {
    if (!config_.hostname.empty()) {
        return config_.hostname;
    }
    
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    
    return "localhost";
}

std::string SyslogSink::get_process_id() const {
    if (!config_.proc_id.empty()) {
        return config_.proc_id;
    }
    
    return std::to_string(getpid());
}

common::Result<void> SyslogSink::send_to_local_syslog(const std::string& message, SyslogPriority priority) {
    try {
        ::syslog(static_cast<int>(priority), "%s", message.c_str());
        statistics_.messages_sent.fetch_add(1);
        statistics_.bytes_sent.fetch_add(message.size());
        return common::Result<void>();  // Success
    } catch (const std::exception& e) {
        return common::Result<void>(
            common::ResultErrorCode::IO_ERROR,
            "Failed to send to local syslog: " + std::string(e.what())
        );
    }
}

common::Result<void> SyslogSink::establish_remote_connection() {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    
    // Close existing connection
    if (remote_socket_ != -1) {
        close(remote_socket_);
        remote_socket_ = -1;
    }
    
    // Create socket
    int sock_type = (config_.transport == SyslogTransport::UDP) ? SOCK_DGRAM : SOCK_STREAM;
    remote_socket_ = socket(AF_INET, sock_type, 0);
    if (remote_socket_ == -1) {
        return common::Result<void>(common::ResultErrorCode::CONNECTION_FAILED, "Failed to create socket");
    }

    // Resolve hostname
    struct hostent* host = gethostbyname(config_.remote_host.c_str());
    if (!host) {
        close(remote_socket_);
        remote_socket_ = -1;
        return common::Result<void>(common::ResultErrorCode::NOT_FOUND, "Failed to resolve hostname: " + config_.remote_host);
    }

    // Setup address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.remote_port);
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    // Connect (for TCP/TLS)
    if (config_.transport != SyslogTransport::UDP) {
        if (connect(remote_socket_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            close(remote_socket_);
            remote_socket_ = -1;
            return common::Result<void>(common::ResultErrorCode::CONNECTION_FAILED, "Failed to connect to remote syslog server");
        }
    }

    return common::Result<void>();  // Success
}

void SyslogSink::close_remote_connection() {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    
    if (remote_socket_ != -1) {
        close(remote_socket_);
        remote_socket_ = -1;
    }
}

void SyslogSink::process_message_batch(const std::vector<common::DataPoint>& messages) {
    for (const auto& message : messages) {
        if (!should_filter_message(message)) {
            auto priority = determine_priority(message);
            std::string formatted_message = format_message(message, priority);
            
            common::Result<void> result;
            if (config_.enable_remote_syslog && !fallback_active_.load()) {
                result = send_to_remote_syslog(formatted_message);
                if (!result.is_success()) {
                    handle_send_failure();
                    result = send_to_fallback(formatted_message);
                }
            } else {
                result = send_to_local_syslog(formatted_message, priority);
            }
            
            if (!result.is_success()) {
                statistics_.messages_failed.fetch_add(1);
            }
        } else {
            statistics_.messages_filtered.fetch_add(1);
        }
    }
}

void SyslogSink::handle_send_failure() {
    uint32_t failures = consecutive_failures_.fetch_add(1) + 1;
    
    if (failures >= config_.fallback_config.max_consecutive_failures) {
        activate_fallback();
    }
    
    statistics_.connection_failures.fetch_add(1);
}

void SyslogSink::activate_fallback() {
    if (!fallback_active_.exchange(true)) {
        statistics_.fallback_activations.fetch_add(1);
        ::syslog(LOG_WARNING, "Activating fallback mode due to remote syslog failures");
    }
}

void SyslogSink::print_statistics() const {
    if (!config_.enable_statistics) {
        return;
    }
    
    auto stats = get_statistics();
    
    ::syslog(LOG_INFO, "Syslog Sink Statistics: processed=%lu, sent=%lu, failed=%lu, filtered=%lu, dropped=%lu, fallback_active=%s",
           stats.messages_processed.load(),
           stats.messages_sent.load(),
           stats.messages_failed.load(),
           stats.messages_filtered.load(),
           stats.messages_dropped.load(),
           fallback_active_.load() ? "true" : "false");
}

// Factory implementations
std::unique_ptr<SyslogSink> SyslogSinkFactory::create(const SyslogSinkConfig& config) {
    return std::make_unique<SyslogSink>(config);
}

std::unique_ptr<SyslogSink> SyslogSinkFactory::create_debug() {
    return std::make_unique<SyslogSink>(SyslogSinkConfig::create_debug());
}

std::unique_ptr<SyslogSink> SyslogSinkFactory::create_production() {
    return std::make_unique<SyslogSink>(SyslogSinkConfig::create_production());
}

std::unique_ptr<SyslogSink> SyslogSinkFactory::create_security() {
    return std::make_unique<SyslogSink>(SyslogSinkConfig::create_security());
}

std::unique_ptr<SyslogSink> SyslogSinkFactory::create_high_volume() {
    return std::make_unique<SyslogSink>(SyslogSinkConfig::create_high_volume());
}

} // namespace ipb::sink::syslog

