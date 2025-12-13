#include "ipb/scoop/console/console_scoop.hpp"

#include <ipb/common/debug.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/platform.hpp>

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>

#include <json/json.h>

namespace ipb::scoop::console {

using namespace common::debug;

namespace {
constexpr const char* LOG_CAT = category::PROTOCOL;
}  // namespace

//=============================================================================
// ConsoleScoopConfig Implementation
//=============================================================================

bool ConsoleScoopConfig::is_valid() const {
    if (buffer_size == 0)
        return false;
    if (format == InputFormat::CSV && csv_columns.empty())
        return false;
    return true;
}

std::string ConsoleScoopConfig::validation_error() const {
    if (buffer_size == 0)
        return "Buffer size must be > 0";
    if (format == InputFormat::CSV && csv_columns.empty())
        return "CSV columns not configured";
    return "";
}

ConsoleScoopConfig ConsoleScoopConfig::create_default() {
    ConsoleScoopConfig config;
    config.format      = InputFormat::AUTO;
    config.interactive = false;
    return config;
}

ConsoleScoopConfig ConsoleScoopConfig::create_interactive() {
    ConsoleScoopConfig config;
    config.format      = InputFormat::AUTO;
    config.interactive = true;
    config.prompt      = "ipb> ";
    config.echo_input  = true;
    return config;
}

ConsoleScoopConfig ConsoleScoopConfig::create_json_pipe() {
    ConsoleScoopConfig config;
    config.format           = InputFormat::JSON;
    config.interactive      = false;
    config.skip_empty_lines = true;
    config.skip_comments    = true;
    return config;
}

ConsoleScoopConfig ConsoleScoopConfig::create_csv_pipe() {
    ConsoleScoopConfig config;
    config.format         = InputFormat::CSV;
    config.interactive    = false;
    config.csv_has_header = true;
    config.csv_columns    = {"address", "value", "quality", "timestamp"};
    return config;
}

//=============================================================================
// ConsoleScoop::Impl
//=============================================================================

class ConsoleScoop::Impl {
public:
    explicit Impl(const ConsoleScoopConfig& config)
        : config_(config), input_stream_(&std::cin), owns_stream_(false), running_(false),
          connected_(false) {
        IPB_LOG_DEBUG(LOG_CAT,
                      "ConsoleScoop::Impl created with format=" << static_cast<int>(config.format));
    }

    Impl(const ConsoleScoopConfig& config, std::istream& input)
        : config_(config), input_stream_(&input), owns_stream_(false), running_(false),
          connected_(false) {
        IPB_LOG_DEBUG(LOG_CAT, "ConsoleScoop::Impl created with custom stream");
    }

    ~Impl() {
        IPB_LOG_TRACE(LOG_CAT, "ConsoleScoop::Impl destructor");
        stop();
    }

    common::Result<> start() {
        IPB_SPAN_CAT("ConsoleScoop::start", LOG_CAT);

        if (IPB_UNLIKELY(running_.load())) {
            IPB_LOG_WARN(LOG_CAT, "ConsoleScoop already running");
            return common::Result<>::success();
        }

        IPB_LOG_INFO(LOG_CAT, "Starting ConsoleScoop...");

        running_.store(true);
        connected_.store(true);

        // Start reader thread
        reader_thread_ = std::thread(&Impl::reader_loop, this);

        // Start processing thread
        processing_thread_ = std::thread(&Impl::processing_loop, this);

        IPB_LOG_INFO(LOG_CAT, "ConsoleScoop started");
        return common::Result<>::success();
    }

    common::Result<> stop() {
        IPB_SPAN_CAT("ConsoleScoop::stop", LOG_CAT);

        if (!running_.load()) {
            return common::Result<>::success();
        }

        IPB_LOG_INFO(LOG_CAT, "Stopping ConsoleScoop...");

        running_.store(false);
        connected_.store(false);

        // Notify threads
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(line_mutex_);
            line_cv_.notify_all();
        }

        // Wait for threads
        if (reader_thread_.joinable()) {
            reader_thread_.join();
        }
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }

        IPB_LOG_INFO(LOG_CAT, "ConsoleScoop stopped");
        return common::Result<>::success();
    }

    bool is_running() const noexcept { return running_.load(); }
    bool is_connected() const noexcept { return connected_.load(); }

    common::Result<common::DataSet> read() {
        std::lock_guard<std::mutex> lock(buffer_mutex_);

        common::DataSet result;
        while (!data_buffer_.empty()) {
            result.add(data_buffer_.front());
            data_buffer_.pop();
        }

        return common::Result<common::DataSet>::success(std::move(result));
    }

    common::Result<> subscribe(DataCallback data_cb, ErrorCallback error_cb) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        data_callback_  = std::move(data_cb);
        error_callback_ = std::move(error_cb);
        IPB_LOG_DEBUG(LOG_CAT, "Callbacks subscribed");
        return common::Result<>::success();
    }

    common::Result<> unsubscribe() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        data_callback_  = nullptr;
        error_callback_ = nullptr;
        IPB_LOG_DEBUG(LOG_CAT, "Callbacks unsubscribed");
        return common::Result<>::success();
    }

    common::Result<> inject_data_point(const common::DataPoint& dp) {
        IPB_LOG_TRACE(LOG_CAT, "Injecting DataPoint: " << dp.get_address());

        std::lock_guard<std::mutex> lock(buffer_mutex_);
        if (data_buffer_.size() < config_.buffer_size) {
            data_buffer_.push(dp);
            stats_.data_points_produced++;
            buffer_cv_.notify_one();
            return common::Result<>::success();
        }

        return common::Result<>::failure("Buffer full");
    }

    common::Result<> inject_line(const std::string& line) {
        IPB_LOG_TRACE(LOG_CAT, "Injecting line: " << line);

        std::lock_guard<std::mutex> lock(line_mutex_);
        line_queue_.push(line);
        line_cv_.notify_one();
        return common::Result<>::success();
    }

    bool is_healthy() const noexcept {
        if (!running_.load())
            return false;
        return stats_.parse_errors.load() < config_.max_parse_errors;
    }

    void reset_statistics() { stats_.reset(); }

    ConsoleScoopStatistics get_console_statistics() const { return stats_; }

    void set_custom_parser(ConsoleScoop::CustomParserCallback parser) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        custom_parser_ = std::move(parser);
    }

    common::Result<> add_address(std::string_view address) {
        std::lock_guard<std::mutex> lock(addresses_mutex_);
        addresses_.emplace_back(address);
        IPB_LOG_DEBUG(LOG_CAT, "Added address filter: " << address);
        return common::Result<>::success();
    }

    common::Result<> remove_address(std::string_view address) {
        std::lock_guard<std::mutex> lock(addresses_mutex_);
        auto it = std::find(addresses_.begin(), addresses_.end(), address);
        if (it != addresses_.end()) {
            addresses_.erase(it);
            IPB_LOG_DEBUG(LOG_CAT, "Removed address filter: " << address);
        }
        return common::Result<>::success();
    }

    std::vector<std::string> get_addresses() const {
        std::lock_guard<std::mutex> lock(addresses_mutex_);
        return addresses_;
    }

private:
    void reader_loop() {
        IPB_LOG_DEBUG(LOG_CAT, "Reader thread started");

        while (running_.load()) {
            // Show prompt in interactive mode
            if (config_.interactive && input_stream_ == &std::cin) {
                std::cout << config_.prompt << std::flush;
            }

            std::string line;
            if (std::getline(*input_stream_, line)) {
                stats_.lines_received++;
                stats_.bytes_received += line.size();

                // Skip empty lines
                if (config_.skip_empty_lines && line.empty()) {
                    stats_.lines_skipped++;
                    continue;
                }

                // Skip comments
                if (config_.skip_comments && !line.empty() && line[0] == '#') {
                    stats_.lines_skipped++;
                    continue;
                }

                // Add to queue
                {
                    std::lock_guard<std::mutex> lock(line_mutex_);
                    line_queue_.push(line);
                    line_cv_.notify_one();
                }
            } else {
                // End of input or error
                if (input_stream_->eof()) {
                    IPB_LOG_INFO(LOG_CAT, "End of input stream");
                    break;
                }
                // Small delay to avoid busy loop on error
                std::this_thread::sleep_for(config_.read_timeout);
            }
        }

        IPB_LOG_DEBUG(LOG_CAT, "Reader thread stopped");
    }

    void processing_loop() {
        IPB_LOG_DEBUG(LOG_CAT, "Processing thread started");

        while (running_.load()) {
            std::string line;

            // Get line from queue
            {
                std::unique_lock<std::mutex> lock(line_mutex_);
                line_cv_.wait_for(lock, std::chrono::milliseconds(100),
                                  [this] { return !line_queue_.empty() || !running_.load(); });

                if (!running_.load() && line_queue_.empty())
                    break;

                if (line_queue_.empty())
                    continue;

                line = std::move(line_queue_.front());
                line_queue_.pop();
            }

            // Parse line
            auto dp_opt = parse_line(line);
            if (!dp_opt) {
                if (!config_.skip_parse_errors) {
                    stats_.parse_errors++;
                    IPB_LOG_WARN(LOG_CAT, "Failed to parse line: " << line);
                }
                continue;
            }

            stats_.lines_processed++;
            stats_.data_points_produced++;

            // Echo if enabled
            if (config_.echo_input) {
                std::cout << "Parsed: address=" << dp_opt->get_address()
                          << " value=" << dp_opt->value_to_string() << std::endl;
            }

            // Buffer or deliver
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                if (data_buffer_.size() < config_.buffer_size) {
                    data_buffer_.push(std::move(*dp_opt));
                } else {
                    stats_.lines_skipped++;
                    IPB_LOG_WARN(LOG_CAT, "Buffer full, dropping data point");
                }
                buffer_cv_.notify_one();
            }

            // Deliver via callback
            deliver_batch();
        }

        IPB_LOG_DEBUG(LOG_CAT, "Processing thread stopped");
    }

    void deliver_batch() {
        std::vector<common::DataPoint> batch;

        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            while (!data_buffer_.empty() && batch.size() < 100) {
                batch.push_back(std::move(data_buffer_.front()));
                data_buffer_.pop();
            }
        }

        if (!batch.empty()) {
            std::lock_guard<std::mutex> cb_lock(callback_mutex_);
            if (data_callback_) {
                common::DataSet ds;
                for (auto& dp : batch) {
                    ds.add(std::move(dp));
                }
                data_callback_(std::move(ds));
            }
        }
    }

    std::optional<common::DataPoint> parse_line(const std::string& line) {
        IPB_LOG_TRACE(LOG_CAT, "Parsing line: " << line);

        // Try custom parser first
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (custom_parser_) {
                auto result = custom_parser_(line);
                if (result)
                    return result;
            }
        }

        InputFormat format = config_.format;

        // Auto-detect format
        if (format == InputFormat::AUTO) {
            format = detect_format(line);
        }

        switch (format) {
            case InputFormat::JSON:
                return parse_json(line);
            case InputFormat::KEY_VALUE:
                return parse_key_value(line);
            case InputFormat::CSV:
                return parse_csv(line);
            default:
                return std::nullopt;
        }
    }

    InputFormat detect_format(const std::string& line) {
        // Trim whitespace
        auto trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

        if (trimmed.empty())
            return InputFormat::KEY_VALUE;

        // Check for JSON
        if (trimmed[0] == '{') {
            return InputFormat::JSON;
        }

        // Check for CSV (contains commas but no '=')
        if (trimmed.find(',') != std::string::npos && trimmed.find('=') == std::string::npos) {
            return InputFormat::CSV;
        }

        // Default to key-value
        return InputFormat::KEY_VALUE;
    }

    std::optional<common::DataPoint> parse_json(const std::string& line) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(line);

        if (!Json::parseFromStream(builder, stream, &root, &errors)) {
            stats_.parse_errors++;
            IPB_LOG_TRACE(LOG_CAT, "JSON parse error: " << errors);
            return std::nullopt;
        }

        // Extract fields
        std::string address = config_.address_prefix;
        if (root.isMember("address") && root["address"].isString()) {
            address += root["address"].asString();
        } else if (root.isMember("name") && root["name"].isString()) {
            address += root["name"].asString();
        } else {
            // Generate address
            address += "unknown_" + std::to_string(stats_.data_points_produced.load());
        }

        common::DataPoint dp;
        dp.set_address(address);
        dp.set_protocol_id(config_.default_protocol_id);

        // Parse value
        if (root.isMember("value")) {
            const auto& val = root["value"];
            if (val.isBool()) {
                dp.set_value(val.asBool());
            } else if (val.isInt64()) {
                dp.set_value(val.asInt64());
            } else if (val.isDouble()) {
                dp.set_value(val.asDouble());
            } else if (val.isString()) {
                dp.set_value(val.asString());
            }
        } else {
            dp.set_value(0.0);  // Default value
        }

        // Parse quality
        if (root.isMember("quality")) {
            const auto& q = root["quality"];
            if (q.isString()) {
                std::string qs = q.asString();
                if (qs == "good" || qs == "Good" || qs == "GOOD") {
                    dp.set_quality(common::Quality::Good);
                } else if (qs == "bad" || qs == "Bad" || qs == "BAD") {
                    dp.set_quality(common::Quality::Bad);
                } else {
                    dp.set_quality(common::Quality::Uncertain);
                }
            } else {
                dp.set_quality(config_.default_quality);
            }
        } else {
            dp.set_quality(config_.default_quality);
        }

        // Parse timestamp
        if (root.isMember("timestamp") && root["timestamp"].isInt64()) {
            auto ts = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(root["timestamp"].asInt64()));
            dp.set_timestamp(ts);
        } else {
            dp.set_timestamp(std::chrono::system_clock::now());
        }

        return dp;
    }

    std::optional<common::DataPoint> parse_key_value(const std::string& line) {
        std::map<std::string, std::string> fields;

        // Parse key=value pairs
        std::regex kv_regex(R"((\w+)=([^\s]+))");
        std::sregex_iterator iter(line.begin(), line.end(), kv_regex);
        std::sregex_iterator end;

        while (iter != end) {
            fields[(*iter)[1].str()] = (*iter)[2].str();
            ++iter;
        }

        if (fields.empty()) {
            // Try simple format: just a value
            try {
                double val = std::stod(line);
                common::DataPoint dp;
                dp.set_address(config_.address_prefix + "input");
                dp.set_value(val);
                dp.set_quality(config_.default_quality);
                dp.set_protocol_id(config_.default_protocol_id);
                dp.set_timestamp(std::chrono::system_clock::now());
                return dp;
            } catch (...) {
                stats_.parse_errors++;
                return std::nullopt;
            }
        }

        // Extract address
        std::string address = config_.address_prefix;
        if (fields.count("address")) {
            address += fields["address"];
        } else if (fields.count("name")) {
            address += fields["name"];
        } else if (fields.count("addr")) {
            address += fields["addr"];
        } else {
            address += "unknown_" + std::to_string(stats_.data_points_produced.load());
        }

        common::DataPoint dp;
        dp.set_address(address);
        dp.set_protocol_id(config_.default_protocol_id);

        // Parse value
        if (fields.count("value") || fields.count("val") || fields.count("v")) {
            std::string val_str = fields.count("value")
                                    ? fields["value"]
                                    : (fields.count("val") ? fields["val"] : fields["v"]);
            try {
                if (val_str == "true" || val_str == "True" || val_str == "TRUE") {
                    dp.set_value(true);
                } else if (val_str == "false" || val_str == "False" || val_str == "FALSE") {
                    dp.set_value(false);
                } else if (val_str.find('.') != std::string::npos) {
                    dp.set_value(std::stod(val_str));
                } else {
                    dp.set_value(std::stoll(val_str));
                }
            } catch (...) {
                dp.set_value(val_str);  // Store as string
            }
        } else {
            dp.set_value(0.0);
        }

        // Parse quality
        if (fields.count("quality") || fields.count("q")) {
            std::string q = fields.count("quality") ? fields["quality"] : fields["q"];
            if (q == "good" || q == "Good" || q == "GOOD") {
                dp.set_quality(common::Quality::Good);
            } else if (q == "bad" || q == "Bad" || q == "BAD") {
                dp.set_quality(common::Quality::Bad);
            } else {
                dp.set_quality(common::Quality::Uncertain);
            }
        } else {
            dp.set_quality(config_.default_quality);
        }

        dp.set_timestamp(std::chrono::system_clock::now());
        return dp;
    }

    std::optional<common::DataPoint> parse_csv(const std::string& line) {
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;

        while (std::getline(ss, field, config_.csv_delimiter)) {
            // Trim whitespace
            field.erase(0, field.find_first_not_of(" \t"));
            field.erase(field.find_last_not_of(" \t") + 1);
            fields.push_back(field);
        }

        if (fields.empty()) {
            stats_.parse_errors++;
            return std::nullopt;
        }

        // Map fields to column names
        std::map<std::string, std::string> named_fields;
        for (size_t i = 0; i < fields.size() && i < config_.csv_columns.size(); ++i) {
            named_fields[config_.csv_columns[i]] = fields[i];
        }

        // Build data point
        common::DataPoint dp;

        // Address
        std::string address = config_.address_prefix;
        if (named_fields.count("address")) {
            address += named_fields["address"];
        } else {
            address += "csv_" + std::to_string(stats_.data_points_produced.load());
        }
        dp.set_address(address);
        dp.set_protocol_id(config_.default_protocol_id);

        // Value
        if (named_fields.count("value")) {
            try {
                dp.set_value(std::stod(named_fields["value"]));
            } catch (...) {
                dp.set_value(named_fields["value"]);
            }
        } else {
            dp.set_value(0.0);
        }

        // Quality
        if (named_fields.count("quality")) {
            std::string q = named_fields["quality"];
            if (q == "good" || q == "Good" || q == "GOOD") {
                dp.set_quality(common::Quality::Good);
            } else if (q == "bad" || q == "Bad" || q == "BAD") {
                dp.set_quality(common::Quality::Bad);
            } else {
                dp.set_quality(common::Quality::Uncertain);
            }
        } else {
            dp.set_quality(config_.default_quality);
        }

        // Timestamp
        if (named_fields.count("timestamp")) {
            try {
                auto ts_ms = std::stoll(named_fields["timestamp"]);
                dp.set_timestamp(
                    std::chrono::system_clock::time_point(std::chrono::milliseconds(ts_ms)));
            } catch (...) {
                dp.set_timestamp(std::chrono::system_clock::now());
            }
        } else {
            dp.set_timestamp(std::chrono::system_clock::now());
        }

        return dp;
    }

    ConsoleScoopConfig config_;
    std::istream* input_stream_;
    bool owns_stream_;

    std::atomic<bool> running_;
    std::atomic<bool> connected_;

    // Line queue
    std::queue<std::string> line_queue_;
    mutable std::mutex line_mutex_;
    std::condition_variable line_cv_;

    // Data buffer
    std::queue<common::DataPoint> data_buffer_;
    mutable std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;

    // Addresses (filters)
    std::vector<std::string> addresses_;
    mutable std::mutex addresses_mutex_;

    // Callbacks
    ConsoleScoop::DataCallback data_callback_;
    ConsoleScoop::ErrorCallback error_callback_;
    ConsoleScoop::CustomParserCallback custom_parser_;
    mutable std::mutex callback_mutex_;

    // Threads
    std::thread reader_thread_;
    std::thread processing_thread_;

    // Statistics
    ConsoleScoopStatistics stats_;
};

//=============================================================================
// ConsoleScoop Implementation
//=============================================================================

ConsoleScoop::ConsoleScoop(const ConsoleScoopConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
    IPB_LOG_INFO(LOG_CAT, "ConsoleScoop created");
}

ConsoleScoop::ConsoleScoop(const ConsoleScoopConfig& config, std::istream& input_stream)
    : impl_(std::make_unique<Impl>(config, input_stream)) {
    IPB_LOG_INFO(LOG_CAT, "ConsoleScoop created with custom stream");
}

ConsoleScoop::~ConsoleScoop() {
    IPB_LOG_INFO(LOG_CAT, "ConsoleScoop destroyed");
}

common::Result<common::DataSet> ConsoleScoop::read() {
    return impl_->read();
}

common::Result<common::DataSet> ConsoleScoop::read_async() {
    return impl_->read();
}

common::Result<> ConsoleScoop::subscribe(DataCallback data_cb, ErrorCallback error_cb) {
    return impl_->subscribe(std::move(data_cb), std::move(error_cb));
}

common::Result<> ConsoleScoop::unsubscribe() {
    return impl_->unsubscribe();
}

common::Result<> ConsoleScoop::add_address(std::string_view address) {
    return impl_->add_address(address);
}

common::Result<> ConsoleScoop::remove_address(std::string_view address) {
    return impl_->remove_address(address);
}

std::vector<std::string> ConsoleScoop::get_addresses() const {
    return impl_->get_addresses();
}

common::Result<> ConsoleScoop::connect() {
    return impl_->start();
}

common::Result<> ConsoleScoop::disconnect() {
    return impl_->stop();
}

bool ConsoleScoop::is_connected() const noexcept {
    return impl_->is_connected();
}

common::Result<> ConsoleScoop::start() {
    return impl_->start();
}

common::Result<> ConsoleScoop::stop() {
    return impl_->stop();
}

bool ConsoleScoop::is_running() const noexcept {
    return impl_->is_running();
}

common::Result<> ConsoleScoop::configure(const common::ConfigurationBase& /*config*/) {
    // Configuration should be done via constructor
    return common::Result<>::success();
}

std::unique_ptr<common::ConfigurationBase> ConsoleScoop::get_configuration() const {
    return nullptr;  // TODO: Implement configuration export
}

common::Statistics ConsoleScoop::get_statistics() const noexcept {
    auto console_stats = impl_->get_console_statistics();
    common::Statistics stats;
    stats.messages_received  = console_stats.lines_received.load();
    stats.messages_processed = console_stats.lines_processed.load();
    stats.messages_dropped   = console_stats.lines_skipped.load();
    stats.errors             = console_stats.parse_errors.load();
    return stats;
}

void ConsoleScoop::reset_statistics() noexcept {
    impl_->reset_statistics();
}

bool ConsoleScoop::is_healthy() const noexcept {
    return impl_->is_healthy();
}

std::string ConsoleScoop::get_health_status() const {
    if (impl_->is_healthy()) {
        return "healthy";
    } else if (!impl_->is_running()) {
        return "stopped";
    } else {
        return "unhealthy: too many parse errors";
    }
}

common::Result<> ConsoleScoop::inject_data_point(const common::DataPoint& dp) {
    return impl_->inject_data_point(dp);
}

common::Result<> ConsoleScoop::inject_line(const std::string& line) {
    return impl_->inject_line(line);
}

ConsoleScoopStatistics ConsoleScoop::get_console_statistics() const {
    return impl_->get_console_statistics();
}

void ConsoleScoop::set_custom_parser(CustomParserCallback parser) {
    impl_->set_custom_parser(std::move(parser));
}

//=============================================================================
// ConsoleScoopFactory Implementation
//=============================================================================

std::unique_ptr<ConsoleScoop> ConsoleScoopFactory::create() {
    return std::make_unique<ConsoleScoop>(ConsoleScoopConfig::create_default());
}

std::unique_ptr<ConsoleScoop> ConsoleScoopFactory::create_interactive(const std::string& prompt) {
    auto config   = ConsoleScoopConfig::create_interactive();
    config.prompt = prompt;
    return std::make_unique<ConsoleScoop>(config);
}

std::unique_ptr<ConsoleScoop> ConsoleScoopFactory::create_json_pipe() {
    return std::make_unique<ConsoleScoop>(ConsoleScoopConfig::create_json_pipe());
}

std::unique_ptr<ConsoleScoop> ConsoleScoopFactory::create_csv_pipe(char delimiter,
                                                                   bool has_header) {
    auto config           = ConsoleScoopConfig::create_csv_pipe();
    config.csv_delimiter  = delimiter;
    config.csv_has_header = has_header;
    return std::make_unique<ConsoleScoop>(config);
}

std::unique_ptr<ConsoleScoop> ConsoleScoopFactory::create_from_stream(std::istream& input) {
    return std::make_unique<ConsoleScoop>(ConsoleScoopConfig::create_default(), input);
}

std::unique_ptr<ConsoleScoop> ConsoleScoopFactory::create(const ConsoleScoopConfig& config) {
    return std::make_unique<ConsoleScoop>(config);
}

}  // namespace ipb::scoop::console
