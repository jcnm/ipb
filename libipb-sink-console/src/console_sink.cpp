#include "ipb/sink/console/console_sink.hpp"
#include <json/json.h>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace ipb::sink::console {

ConsoleSink::ConsoleSink(const ConsoleSinkConfig& config) 
    : config_(config) {
    compile_address_filters();
}

ConsoleSink::~ConsoleSink() {
    if (running_.load()) {
        stop();
    }
    shutdown();
}

common::Result<void> ConsoleSink::initialize(const std::string& config_path) {
    try {
        // If config file is provided, load it
        if (!config_path.empty()) {
            // TODO: Load configuration from YAML file
            // For now, use default configuration
        }
        
        // Initialize file output if enabled
        if (config_.enable_file_output && !config_.output_file_path.empty()) {
            file_stream_ = std::make_unique<std::ofstream>(
                config_.output_file_path, 
                std::ios::out | std::ios::app
            );
            
            if (!file_stream_->is_open()) {
                return common::Result<void>::failure(
                    "Failed to open output file: " + config_.output_file_path
                );
            }
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to initialize console sink: " + std::string(e.what())
        );
    }
}

common::Result<void> ConsoleSink::start() {
    if (running_.load()) {
        return common::Result<void>::failure("Console sink is already running");
    }
    
    try {
        running_.store(true);
        shutdown_requested_.store(false);
        
        // Start worker thread for async processing
        if (config_.enable_async_output) {
            worker_thread_ = std::thread(&ConsoleSink::worker_loop, this);
        }
        
        // Start statistics thread if enabled
        if (config_.enable_statistics) {
            statistics_thread_ = std::thread(&ConsoleSink::statistics_loop, this);
        }
        
        statistics_.reset();
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        running_.store(false);
        return common::Result<void>::failure(
            "Failed to start console sink: " + std::string(e.what())
        );
    }
}

common::Result<void> ConsoleSink::stop() {
    if (!running_.load()) {
        return common::Result<void>::success();
    }
    
    try {
        running_.store(false);
        
        // Wake up worker thread
        if (config_.enable_async_output) {
            queue_condition_.notify_all();
            
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
        }
        
        // Stop statistics thread
        if (statistics_thread_.joinable()) {
            statistics_thread_.join();
        }
        
        // Flush any remaining output
        flush();
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to stop console sink: " + std::string(e.what())
        );
    }
}

common::Result<void> ConsoleSink::shutdown() {
    shutdown_requested_.store(true);
    
    auto stop_result = stop();
    if (!stop_result.is_success()) {
        return stop_result;
    }
    
    try {
        // Close file stream
        if (file_stream_ && file_stream_->is_open()) {
            file_stream_->close();
        }
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to shutdown console sink: " + std::string(e.what())
        );
    }
}

common::Result<void> ConsoleSink::send_data_point(const common::DataPoint& data_point) {
    if (!running_.load()) {
        return common::Result<void>::failure("Console sink is not running");
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // Apply filtering
        if (should_filter_message(data_point)) {
            statistics_.messages_filtered.fetch_add(1);
            return common::Result<void>::success();
        }
        
        if (config_.enable_async_output) {
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
            std::string formatted_message = format_message(data_point);
            write_output(formatted_message);
        }
        
        statistics_.messages_processed.fetch_add(1);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time
        );
        statistics_.update_processing_time(processing_time);
        
        return common::Result<void>::success();
        
    } catch (const std::exception& e) {
        return common::Result<void>::failure(
            "Failed to send data point: " + std::string(e.what())
        );
    }
}

common::Result<void> ConsoleSink::send_data_set(const common::DataSet& data_set) {
    if (!running_.load()) {
        return common::Result<void>::failure("Console sink is not running");
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

bool ConsoleSink::is_connected() const {
    return running_.load();
}

bool ConsoleSink::is_healthy() const {
    if (!running_.load()) {
        return false;
    }
    
    // Check if file output is healthy (if enabled)
    if (config_.enable_file_output && file_stream_) {
        return file_stream_->good();
    }
    
    return true;
}

common::SinkMetrics ConsoleSink::get_metrics() const {
    common::SinkMetrics metrics;
    metrics.sink_id = "console_sink";
    metrics.messages_sent = statistics_.messages_processed.load();
    metrics.messages_failed = statistics_.messages_dropped.load();
    metrics.bytes_sent = statistics_.bytes_written.load();
    metrics.is_connected = is_connected();
    metrics.is_healthy = is_healthy();
    metrics.avg_processing_time = statistics_.get_average_processing_time();
    
    return metrics;
}

std::string ConsoleSink::get_sink_info() const {
    Json::Value info;
    info["type"] = "console";
    info["format"] = static_cast<int>(config_.output_format);
    info["async_enabled"] = config_.enable_async_output;
    info["file_output_enabled"] = config_.enable_file_output;
    info["filtering_enabled"] = config_.enable_filtering;
    info["statistics_enabled"] = config_.enable_statistics;
    
    if (config_.enable_file_output) {
        info["output_file"] = config_.output_file_path;
    }
    
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, info);
}

void ConsoleSink::worker_loop() {
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

void ConsoleSink::statistics_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(config_.statistics_interval);
        
        if (running_.load()) {
            print_statistics();
        }
    }
}

bool ConsoleSink::should_filter_message(const common::DataPoint& data_point) const {
    if (!config_.enable_filtering) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(filter_mutex_);
    
    // Check address filters
    if (!address_regex_filters_.empty()) {
        bool address_match = false;
        for (const auto& regex : address_regex_filters_) {
            if (std::regex_match(data_point.get_address(), regex)) {
                address_match = true;
                break;
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

std::string ConsoleSink::format_message(const common::DataPoint& data_point) const {
    switch (config_.output_format) {
        case OutputFormat::PLAIN:
            return format_plain(data_point);
        case OutputFormat::JSON:
            return format_json(data_point);
        case OutputFormat::CSV:
            return format_csv(data_point);
        case OutputFormat::TABLE:
            return format_table(data_point);
        case OutputFormat::COLORED:
            return format_colored(data_point);
        case OutputFormat::CUSTOM:
            if (config_.custom_formatter) {
                return config_.custom_formatter(data_point);
            }
            return format_plain(data_point);
        default:
            return format_plain(data_point);
    }
}

std::string ConsoleSink::format_plain(const common::DataPoint& data_point) const {
    std::ostringstream oss;
    
    oss << config_.line_prefix;
    
    if (config_.include_timestamp) {
        oss << format_timestamp(data_point.get_timestamp()) << config_.field_separator;
    }
    
    if (config_.include_protocol_id) {
        oss << "P" << data_point.get_protocol_id() << config_.field_separator;
    }
    
    if (config_.include_address) {
        oss << data_point.get_address() << config_.field_separator;
    }
    
    if (config_.include_value && data_point.get_value().has_value()) {
        oss << format_value(data_point.get_value().value()) << config_.field_separator;
    }
    
    if (config_.include_quality) {
        oss << format_quality(data_point.get_quality());
    }
    
    oss << config_.line_suffix;
    
    return oss.str();
}

std::string ConsoleSink::format_json(const common::DataPoint& data_point) const {
    Json::Value json;
    
    if (config_.include_timestamp) {
        json["timestamp"] = format_timestamp(data_point.get_timestamp());
    }
    
    if (config_.include_protocol_id) {
        json["protocol_id"] = data_point.get_protocol_id();
    }
    
    if (config_.include_address) {
        json["address"] = data_point.get_address();
    }
    
    if (config_.include_value && data_point.get_value().has_value()) {
        json["value"] = format_value(data_point.get_value().value());
    }
    
    if (config_.include_quality) {
        json["quality"] = format_quality(data_point.get_quality());
    }
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, json) + config_.line_suffix;
}

std::string ConsoleSink::format_csv(const common::DataPoint& data_point) const {
    std::ostringstream oss;
    
    if (config_.include_timestamp) {
        oss << "\"" << format_timestamp(data_point.get_timestamp()) << "\",";
    }
    
    if (config_.include_protocol_id) {
        oss << data_point.get_protocol_id() << ",";
    }
    
    if (config_.include_address) {
        oss << "\"" << data_point.get_address() << "\",";
    }
    
    if (config_.include_value && data_point.get_value().has_value()) {
        oss << "\"" << format_value(data_point.get_value().value()) << "\",";
    }
    
    if (config_.include_quality) {
        oss << "\"" << format_quality(data_point.get_quality()) << "\"";
    }
    
    oss << config_.line_suffix;
    
    return oss.str();
}

std::string ConsoleSink::format_table(const common::DataPoint& data_point) const {
    std::ostringstream oss;
    
    oss << "| ";
    
    if (config_.include_timestamp) {
        oss << std::setw(23) << std::left << format_timestamp(data_point.get_timestamp()) << " | ";
    }
    
    if (config_.include_protocol_id) {
        oss << std::setw(3) << std::right << data_point.get_protocol_id() << " | ";
    }
    
    if (config_.include_address) {
        oss << std::setw(30) << std::left << data_point.get_address() << " | ";
    }
    
    if (config_.include_value && data_point.get_value().has_value()) {
        oss << std::setw(15) << std::right << format_value(data_point.get_value().value()) << " | ";
    }
    
    if (config_.include_quality) {
        oss << std::setw(10) << std::left << format_quality(data_point.get_quality()) << " |";
    }
    
    oss << config_.line_suffix;
    
    return oss.str();
}

std::string ConsoleSink::format_colored(const common::DataPoint& data_point) const {
    std::ostringstream oss;
    
    oss << config_.line_prefix;
    
    if (config_.include_timestamp) {
        oss << apply_color(format_timestamp(data_point.get_timestamp()), config_.timestamp_color) 
            << config_.field_separator;
    }
    
    if (config_.include_protocol_id) {
        oss << apply_color("P" + std::to_string(data_point.get_protocol_id()), config_.protocol_color) 
            << config_.field_separator;
    }
    
    if (config_.include_address) {
        oss << apply_color(data_point.get_address(), config_.address_color) << config_.field_separator;
    }
    
    if (config_.include_value && data_point.get_value().has_value()) {
        oss << apply_color(format_value(data_point.get_value().value()), config_.value_color) 
            << config_.field_separator;
    }
    
    if (config_.include_quality) {
        ConsoleColor quality_color;
        switch (data_point.get_quality()) {
            case common::Quality::GOOD:
                quality_color = config_.quality_good_color;
                break;
            case common::Quality::UNCERTAIN:
                quality_color = config_.quality_uncertain_color;
                break;
            case common::Quality::BAD:
                quality_color = config_.quality_bad_color;
                break;
            default:
                quality_color = ConsoleColor::WHITE;
                break;
        }
        oss << apply_color(format_quality(data_point.get_quality()), quality_color);
    }
    
    oss << apply_color("", ConsoleColor::RESET) << config_.line_suffix;
    
    return oss.str();
}

std::string ConsoleSink::format_timestamp(const common::Timestamp& timestamp) const {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()
    ) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string ConsoleSink::format_value(const common::Value& value) const {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i > 0) oss << ",";
                oss << static_cast<int>(v[i]);
            }
            oss << "]";
            return oss.str();
        } else {
            return std::to_string(v);
        }
    }, value);
}

std::string ConsoleSink::format_quality(common::Quality quality) const {
    switch (quality) {
        case common::Quality::GOOD:
            return "GOOD";
        case common::Quality::UNCERTAIN:
            return "UNCERTAIN";
        case common::Quality::BAD:
            return "BAD";
        case common::Quality::UNKNOWN:
            return "UNKNOWN";
        default:
            return "INVALID";
    }
}

std::string ConsoleSink::apply_color(const std::string& text, ConsoleColor color) const {
    if (!config_.enable_colors) {
        return text;
    }
    
    return "\033[" + std::to_string(static_cast<int>(color)) + "m" + text + "\033[0m";
}

void ConsoleSink::write_output(const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    
    // Write to console
    if (config_.enable_console_output && config_.output_stream) {
        *config_.output_stream << message;
        config_.output_stream->flush();
    }
    
    // Write to file
    if (config_.enable_file_output && file_stream_ && file_stream_->is_open()) {
        *file_stream_ << message;
        file_stream_->flush();
    }
    
    statistics_.bytes_written.fetch_add(message.size());
    statistics_.flush_operations.fetch_add(1);
}

void ConsoleSink::process_message_batch(const std::vector<common::DataPoint>& messages) {
    std::ostringstream batch_output;
    
    for (const auto& message : messages) {
        if (!should_filter_message(message)) {
            batch_output << format_message(message);
        } else {
            statistics_.messages_filtered.fetch_add(1);
        }
    }
    
    if (!batch_output.str().empty()) {
        write_output(batch_output.str());
    }
}

void ConsoleSink::compile_address_filters() {
    std::lock_guard<std::mutex> lock(filter_mutex_);
    
    address_regex_filters_.clear();
    
    for (const auto& pattern : config_.address_filters) {
        try {
            address_regex_filters_.emplace_back(pattern);
        } catch (const std::regex_error& e) {
            // Log error but continue with other patterns
            std::cerr << "Invalid regex pattern: " << pattern << " - " << e.what() << std::endl;
        }
    }
}

void ConsoleSink::print_statistics() const {
    if (!config_.enable_statistics) {
        return;
    }
    
    auto stats = get_statistics();
    
    std::ostringstream oss;
    oss << "\n=== Console Sink Statistics ===\n";
    oss << "Messages processed: " << stats.messages_processed.load() << "\n";
    oss << "Messages filtered: " << stats.messages_filtered.load() << "\n";
    oss << "Messages dropped: " << stats.messages_dropped.load() << "\n";
    oss << "Bytes written: " << stats.bytes_written.load() << "\n";
    oss << "Messages/sec: " << std::fixed << std::setprecision(2) << stats.get_messages_per_second() << "\n";
    oss << "Avg processing time: " << stats.get_average_processing_time().count() << " ns\n";
    oss << "Min processing time: " << stats.min_processing_time.load().count() << " ns\n";
    oss << "Max processing time: " << stats.max_processing_time.load().count() << " ns\n";
    oss << "===============================\n";
    
    write_output(oss.str());
}

// ConsoleSinkFactory implementation

std::unique_ptr<ConsoleSink> ConsoleSinkFactory::create(const ConsoleSinkConfig& config) {
    return std::make_unique<ConsoleSink>(config);
}

std::unique_ptr<ConsoleSink> ConsoleSinkFactory::create_from_file(const std::string& config_file) {
    // TODO: Load configuration from file
    ConsoleSinkConfig config;
    auto sink = std::make_unique<ConsoleSink>(config);
    sink->initialize(config_file);
    return sink;
}

std::unique_ptr<ConsoleSink> ConsoleSinkFactory::create_debug() {
    return std::make_unique<ConsoleSink>(ConsoleSinkConfig::create_debug());
}

std::unique_ptr<ConsoleSink> ConsoleSinkFactory::create_production() {
    return std::make_unique<ConsoleSink>(ConsoleSinkConfig::create_production());
}

std::unique_ptr<ConsoleSink> ConsoleSinkFactory::create_minimal() {
    return std::make_unique<ConsoleSink>(ConsoleSinkConfig::create_minimal());
}

std::unique_ptr<ConsoleSink> ConsoleSinkFactory::create_verbose() {
    return std::make_unique<ConsoleSink>(ConsoleSinkConfig::create_verbose());
}

} // namespace ipb::sink::console

