#pragma once

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include <syslog.h>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <map>

namespace ipb::sink::syslog {

/**
 * @brief Syslog facilities
 */
enum class SyslogFacility {
    KERN = LOG_KERN,           ///< Kernel messages
    USER = LOG_USER,           ///< User-level messages
    MAIL = LOG_MAIL,           ///< Mail system
    DAEMON = LOG_DAEMON,       ///< System daemons
    AUTH = LOG_AUTH,           ///< Security/authorization messages
    SYSLOG = LOG_SYSLOG,       ///< Messages generated internally by syslogd
    LPR = LOG_LPR,             ///< Line printer subsystem
    NEWS = LOG_NEWS,           ///< Network news subsystem
    UUCP = LOG_UUCP,           ///< UUCP subsystem
    CRON = LOG_CRON,           ///< Clock daemon
    AUTHPRIV = LOG_AUTHPRIV,   ///< Security/authorization messages (private)
    FTP = LOG_FTP,             ///< FTP daemon
    LOCAL0 = LOG_LOCAL0,       ///< Local use facility 0
    LOCAL1 = LOG_LOCAL1,       ///< Local use facility 1
    LOCAL2 = LOG_LOCAL2,       ///< Local use facility 2
    LOCAL3 = LOG_LOCAL3,       ///< Local use facility 3
    LOCAL4 = LOG_LOCAL4,       ///< Local use facility 4
    LOCAL5 = LOG_LOCAL5,       ///< Local use facility 5
    LOCAL6 = LOG_LOCAL6,       ///< Local use facility 6
    LOCAL7 = LOG_LOCAL7        ///< Local use facility 7
};

/**
 * @brief Syslog priorities/severities
 */
enum class SyslogPriority {
    EMERG = LOG_EMERG,         ///< System is unusable
    ALERT = LOG_ALERT,         ///< Action must be taken immediately
    CRIT = LOG_CRIT,           ///< Critical conditions
    ERR = LOG_ERR,             ///< Error conditions
    WARNING = LOG_WARNING,     ///< Warning conditions
    NOTICE = LOG_NOTICE,       ///< Normal but significant condition
    INFO = LOG_INFO,           ///< Informational messages
    DEBUG = LOG_DEBUG          ///< Debug-level messages
};

/**
 * @brief Syslog message formats
 */
enum class SyslogFormat {
    RFC3164,    ///< Traditional syslog format (RFC 3164)
    RFC5424,    ///< New syslog format (RFC 5424)
    CEF,        ///< Common Event Format
    LEEF,       ///< Log Event Extended Format
    JSON,       ///< JSON format
    PLAIN       ///< Plain text format
};

/**
 * @brief Remote syslog transport protocols
 */
enum class SyslogTransport {
    UDP,        ///< UDP transport (traditional)
    TCP,        ///< TCP transport (reliable)
    TLS         ///< TLS transport (secure)
};

/**
 * @brief Priority mapping configuration
 */
struct PriorityMapping {
    std::map<std::string, SyslogPriority> address_priority_map;
    std::map<uint16_t, SyslogPriority> protocol_priority_map;
    std::map<common::Quality, SyslogPriority> quality_priority_map;
    SyslogPriority default_priority = SyslogPriority::INFO;
    
    // Custom priority callback
    std::function<SyslogPriority(const common::DataPoint&)> custom_priority_callback;
};

/**
 * @brief Fallback configuration
 */
struct FallbackConfig {
    bool enable_console_fallback = true;
    bool enable_file_fallback = true;
    std::string fallback_file_path = "/var/log/ipb-syslog-fallback.log";
    bool enable_remote_fallback = false;
    std::string remote_fallback_host;
    uint16_t remote_fallback_port = 514;
    
    // Fallback triggers
    uint32_t max_consecutive_failures = 10;
    std::chrono::seconds failure_timeout{30};
    std::chrono::seconds recovery_check_interval{60};
};

/**
 * @brief Configuration for syslog sink
 */
struct SyslogSinkConfig {
    // Basic syslog settings
    std::string ident = "ipb-gateway";
    SyslogFacility facility = SyslogFacility::LOCAL0;
    SyslogFormat format = SyslogFormat::RFC5424;
    bool include_pid = true;
    bool log_to_stderr = false;
    bool log_perror = false;
    
    // Remote syslog settings
    bool enable_remote_syslog = false;
    std::string remote_host;
    uint16_t remote_port = 514;
    SyslogTransport transport = SyslogTransport::UDP;
    
    // TLS settings (for TLS transport)
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
    bool verify_server_cert = true;
    
    // Message formatting
    std::string hostname;  // Auto-detected if empty
    std::string app_name = "ipb-gateway";
    std::string proc_id;   // Auto-detected if empty
    std::string msg_id = "IPB-DATA";
    
    // Priority mapping
    PriorityMapping priority_mapping;
    
    // Performance settings
    bool enable_async_logging = true;
    size_t queue_size = 10000;
    std::chrono::milliseconds flush_interval{100};
    size_t batch_size = 100;
    
    // Filtering settings
    bool enable_filtering = false;
    std::vector<std::string> address_filters;
    std::vector<uint16_t> protocol_id_filters;
    std::vector<common::Quality> quality_filters;
    
    // Fallback configuration
    FallbackConfig fallback_config;
    
    // Statistics settings
    bool enable_statistics = false;
    std::chrono::seconds statistics_interval{60};
    
    /**
     * @brief Create configuration for debug mode
     */
    static SyslogSinkConfig create_debug() {
        SyslogSinkConfig config;
        config.facility = SyslogFacility::LOCAL7;
        config.format = SyslogFormat::PLAIN;
        config.enable_statistics = true;
        config.enable_async_logging = true;
        config.log_perror = true;  // Also log to stderr for debugging
        return config;
    }
    
    /**
     * @brief Create configuration for production mode
     */
    static SyslogSinkConfig create_production() {
        SyslogSinkConfig config;
        config.facility = SyslogFacility::DAEMON;
        config.format = SyslogFormat::RFC5424;
        config.enable_remote_syslog = true;
        config.transport = SyslogTransport::TLS;
        config.enable_async_logging = true;
        config.enable_statistics = false;
        return config;
    }
    
    /**
     * @brief Create configuration for security logging
     */
    static SyslogSinkConfig create_security() {
        SyslogSinkConfig config;
        config.facility = SyslogFacility::AUTHPRIV;
        config.format = SyslogFormat::CEF;
        config.enable_remote_syslog = true;
        config.transport = SyslogTransport::TLS;
        config.verify_server_cert = true;
        config.enable_async_logging = false;  // Synchronous for security events
        return config;
    }
    
    /**
     * @brief Create configuration for high volume logging
     */
    static SyslogSinkConfig create_high_volume() {
        SyslogSinkConfig config;
        config.facility = SyslogFacility::LOCAL0;
        config.format = SyslogFormat::JSON;
        config.enable_remote_syslog = true;
        config.transport = SyslogTransport::TCP;
        config.enable_async_logging = true;
        config.queue_size = 50000;
        config.batch_size = 1000;
        config.flush_interval = std::chrono::milliseconds{50};
        return config;
    }
};

/**
 * @brief Statistics for syslog sink performance monitoring
 */
struct SyslogSinkStatistics {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> messages_filtered{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_failed{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> connection_failures{0};
    std::atomic<uint64_t> fallback_activations{0};
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::nanoseconds total_processing_time{0};
    std::chrono::nanoseconds min_processing_time{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_processing_time{0};
    
    mutable std::mutex stats_mutex;
    
    SyslogSinkStatistics() : start_time(std::chrono::steady_clock::now()) {}

    // Copy constructor - needed for returning statistics
    SyslogSinkStatistics(const SyslogSinkStatistics& other)
        : messages_processed(other.messages_processed.load())
        , messages_filtered(other.messages_filtered.load())
        , messages_dropped(other.messages_dropped.load())
        , messages_sent(other.messages_sent.load())
        , messages_failed(other.messages_failed.load())
        , bytes_sent(other.bytes_sent.load())
        , connection_failures(other.connection_failures.load())
        , fallback_activations(other.fallback_activations.load())
        , start_time(other.start_time)
        , total_processing_time(other.total_processing_time)
        , min_processing_time(other.min_processing_time)
        , max_processing_time(other.max_processing_time) {}

    // Move constructor
    SyslogSinkStatistics(SyslogSinkStatistics&& other) noexcept
        : messages_processed(other.messages_processed.load())
        , messages_filtered(other.messages_filtered.load())
        , messages_dropped(other.messages_dropped.load())
        , messages_sent(other.messages_sent.load())
        , messages_failed(other.messages_failed.load())
        , bytes_sent(other.bytes_sent.load())
        , connection_failures(other.connection_failures.load())
        , fallback_activations(other.fallback_activations.load())
        , start_time(other.start_time)
        , total_processing_time(other.total_processing_time)
        , min_processing_time(other.min_processing_time)
        , max_processing_time(other.max_processing_time) {}

    // Copy assignment
    SyslogSinkStatistics& operator=(const SyslogSinkStatistics& other) {
        if (this != &other) {
            messages_processed.store(other.messages_processed.load());
            messages_filtered.store(other.messages_filtered.load());
            messages_dropped.store(other.messages_dropped.load());
            messages_sent.store(other.messages_sent.load());
            messages_failed.store(other.messages_failed.load());
            bytes_sent.store(other.bytes_sent.load());
            connection_failures.store(other.connection_failures.load());
            fallback_activations.store(other.fallback_activations.load());
            start_time = other.start_time;
            total_processing_time = other.total_processing_time;
            min_processing_time = other.min_processing_time;
            max_processing_time = other.max_processing_time;
        }
        return *this;
    }

    /**
     * @brief Get messages per second
     */
    double get_messages_per_second() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        return seconds > 0 ? static_cast<double>(messages_processed.load()) / seconds : 0.0;
    }
    
    /**
     * @brief Get average processing time
     */
    std::chrono::nanoseconds get_average_processing_time() const {
        auto processed = messages_processed.load();
        if (processed > 0) {
            return std::chrono::nanoseconds{total_processing_time.count() / static_cast<int64_t>(processed)};
        }
        return std::chrono::nanoseconds{0};
    }
    
    /**
     * @brief Update processing time statistics
     */
    void update_processing_time(std::chrono::nanoseconds processing_time) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        total_processing_time += processing_time;
        min_processing_time = std::min(min_processing_time, processing_time);
        max_processing_time = std::max(max_processing_time, processing_time);
    }
    
    /**
     * @brief Reset all statistics
     */
    void reset() {
        messages_processed = 0;
        messages_filtered = 0;
        messages_dropped = 0;
        messages_sent = 0;
        messages_failed = 0;
        bytes_sent = 0;
        connection_failures = 0;
        fallback_activations = 0;
        start_time = std::chrono::steady_clock::now();
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        total_processing_time = std::chrono::nanoseconds{0};
        min_processing_time = std::chrono::nanoseconds::max();
        max_processing_time = std::chrono::nanoseconds{0};
    }
};

/**
 * @brief High-performance syslog sink for system integration
 */
class SyslogSink : public common::ISink {
public:
    /**
     * @brief Constructor with configuration
     */
    explicit SyslogSink(const SyslogSinkConfig& config = SyslogSinkConfig{});
    
    /**
     * @brief Destructor
     */
    ~SyslogSink() override;
    
    // IIPBSink interface implementation
    common::Result<void> initialize(const std::string& config_path) override;
    common::Result<void> start() override;
    common::Result<void> stop() override;
    common::Result<void> shutdown() override;
    
    common::Result<void> send_data_point(const common::DataPoint& data_point) override;
    common::Result<void> send_data_set(const common::DataSet& data_set) override;
    
    bool is_connected() const override;
    bool is_healthy() const override;
    
    common::SinkMetrics get_metrics() const override;
    std::string get_sink_info() const override;
    
    // Syslog-specific methods
    
    /**
     * @brief Update configuration at runtime
     */
    common::Result<void> update_config(const SyslogSinkConfig& new_config);
    
    /**
     * @brief Add address filter pattern
     */
    void add_address_filter(const std::string& pattern);
    
    /**
     * @brief Remove address filter pattern
     */
    void remove_address_filter(const std::string& pattern);
    
    /**
     * @brief Clear all filters
     */
    void clear_filters();
    
    /**
     * @brief Get current statistics
     */
    SyslogSinkStatistics get_statistics() const;
    
    /**
     * @brief Reset statistics
     */
    void reset_statistics();
    
    /**
     * @brief Test connection to remote syslog server
     */
    common::Result<void> test_connection();
    
    /**
     * @brief Force fallback mode
     */
    void activate_fallback();
    
    /**
     * @brief Attempt to recover from fallback mode
     */
    common::Result<void> recover_from_fallback();

private:
    SyslogSinkConfig config_;
    SyslogSinkStatistics statistics_;
    
    // State management
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<bool> fallback_active_{false};
    std::atomic<uint32_t> consecutive_failures_{0};
    
    // Threading
    std::thread worker_thread_;
    std::thread statistics_thread_;
    std::thread recovery_thread_;
    
    // Async processing
    std::queue<common::DataPoint> message_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    
    // Remote syslog connection
    int remote_socket_{-1};
    mutable std::mutex connection_mutex_;
    
    // Fallback streams
    std::unique_ptr<std::ofstream> fallback_file_stream_;
    mutable std::mutex fallback_mutex_;
    
    // Internal methods
    void worker_loop();
    void statistics_loop();
    void recovery_loop();
    
    bool should_filter_message(const common::DataPoint& data_point) const;
    SyslogPriority determine_priority(const common::DataPoint& data_point) const;
    
    std::string format_message(const common::DataPoint& data_point, SyslogPriority priority) const;
    std::string format_rfc3164(const common::DataPoint& data_point, SyslogPriority priority) const;
    std::string format_rfc5424(const common::DataPoint& data_point, SyslogPriority priority) const;
    std::string format_cef(const common::DataPoint& data_point, SyslogPriority priority) const;
    std::string format_leef(const common::DataPoint& data_point, SyslogPriority priority) const;
    std::string format_json(const common::DataPoint& data_point, SyslogPriority priority) const;
    std::string format_plain(const common::DataPoint& data_point, SyslogPriority priority) const;
    
    std::string format_timestamp_rfc3164() const;
    std::string format_timestamp_rfc5424() const;
    std::string get_hostname() const;
    std::string get_process_id() const;
    
    common::Result<void> send_to_local_syslog(const std::string& message, SyslogPriority priority);
    common::Result<void> send_to_remote_syslog(const std::string& message);
    common::Result<void> send_to_fallback(const std::string& message);
    
    common::Result<void> establish_remote_connection();
    void close_remote_connection();
    
    void process_message_batch(const std::vector<common::DataPoint>& messages);
    void handle_send_failure();
    
    void print_statistics() const;
};

/**
 * @brief Factory for creating syslog sinks
 */
class SyslogSinkFactory {
public:
    /**
     * @brief Create syslog sink with configuration
     */
    static std::unique_ptr<SyslogSink> create(const SyslogSinkConfig& config = SyslogSinkConfig{});
    
    /**
     * @brief Create syslog sink from configuration file
     */
    static std::unique_ptr<SyslogSink> create_from_file(const std::string& config_file);
    
    /**
     * @brief Create debug syslog sink
     */
    static std::unique_ptr<SyslogSink> create_debug();
    
    /**
     * @brief Create production syslog sink
     */
    static std::unique_ptr<SyslogSink> create_production();
    
    /**
     * @brief Create security syslog sink
     */
    static std::unique_ptr<SyslogSink> create_security();
    
    /**
     * @brief Create high volume syslog sink
     */
    static std::unique_ptr<SyslogSink> create_high_volume();
};

} // namespace ipb::sink::syslog

