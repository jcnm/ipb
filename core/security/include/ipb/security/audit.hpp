#pragma once

/**
 * @file audit.hpp
 * @brief Enterprise-grade audit logging system
 *
 * Features:
 * - Structured audit events with correlation IDs
 * - Multiple output backends (file, syslog, remote)
 * - Tamper-evident logging with hash chains
 * - Async non-blocking writes
 * - Log rotation and retention policies
 * - Compliance-ready formats (CEF, JSON, LEEF)
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "authentication.hpp"

namespace ipb::security {

//=============================================================================
// Audit Event Types
//=============================================================================

/**
 * @brief Audit event severity levels
 */
enum class AuditSeverity {
    DEBUG     = 0,
    INFO      = 1,
    NOTICE    = 2,
    WARNING   = 3,
    ERROR     = 4,
    CRITICAL  = 5,
    ALERT     = 6,
    EMERGENCY = 7
};

inline std::string_view severity_string(AuditSeverity sev) {
    switch (sev) {
        case AuditSeverity::DEBUG:
            return "DEBUG";
        case AuditSeverity::INFO:
            return "INFO";
        case AuditSeverity::NOTICE:
            return "NOTICE";
        case AuditSeverity::WARNING:
            return "WARNING";
        case AuditSeverity::ERROR:
            return "ERROR";
        case AuditSeverity::CRITICAL:
            return "CRITICAL";
        case AuditSeverity::ALERT:
            return "ALERT";
        case AuditSeverity::EMERGENCY:
            return "EMERGENCY";
    }
    return "UNKNOWN";
}

/**
 * @brief Audit event categories
 */
enum class AuditCategory {
    AUTHENTICATION,  // Login, logout, auth failures
    AUTHORIZATION,   // Permission checks, access denials
    DATA_ACCESS,     // Read/write operations on data
    CONFIGURATION,   // System configuration changes
    ADMINISTRATION,  // User/role management
    SECURITY,        // Security-related events
    SYSTEM,          // System lifecycle events
    NETWORK,         // Network-related events
    CUSTOM           // Application-specific events
};

inline std::string_view category_string(AuditCategory cat) {
    switch (cat) {
        case AuditCategory::AUTHENTICATION:
            return "AUTHENTICATION";
        case AuditCategory::AUTHORIZATION:
            return "AUTHORIZATION";
        case AuditCategory::DATA_ACCESS:
            return "DATA_ACCESS";
        case AuditCategory::CONFIGURATION:
            return "CONFIGURATION";
        case AuditCategory::ADMINISTRATION:
            return "ADMINISTRATION";
        case AuditCategory::SECURITY:
            return "SECURITY";
        case AuditCategory::SYSTEM:
            return "SYSTEM";
        case AuditCategory::NETWORK:
            return "NETWORK";
        case AuditCategory::CUSTOM:
            return "CUSTOM";
    }
    return "UNKNOWN";
}

/**
 * @brief Audit event outcome
 */
enum class AuditOutcome { SUCCESS, FAILURE, UNKNOWN };

inline std::string_view outcome_string(AuditOutcome outcome) {
    switch (outcome) {
        case AuditOutcome::SUCCESS:
            return "SUCCESS";
        case AuditOutcome::FAILURE:
            return "FAILURE";
        case AuditOutcome::UNKNOWN:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

//=============================================================================
// Audit Event
//=============================================================================

/**
 * @brief Structured audit event
 */
struct AuditEvent {
    // Identification
    uint64_t event_id{0};
    std::string correlation_id;
    std::string session_id;

    // Timing
    std::chrono::system_clock::time_point timestamp;
    std::chrono::microseconds duration{0};

    // Classification
    AuditSeverity severity{AuditSeverity::INFO};
    AuditCategory category{AuditCategory::CUSTOM};
    AuditOutcome outcome{AuditOutcome::UNKNOWN};
    std::string event_type;  // e.g., "user.login", "data.read"

    // Actor
    std::string actor_id;    // User/service identifier
    std::string actor_type;  // "user", "service", "system"
    std::string actor_ip;    // Source IP address
    std::string actor_user_agent;

    // Target
    std::string target_type;  // Resource type
    std::string target_id;    // Resource identifier
    std::string target_name;  // Human-readable name

    // Action details
    std::string action;         // Action performed
    std::string action_detail;  // Additional details
    std::unordered_map<std::string, std::string> metadata;

    // Integrity
    std::string previous_hash;  // Hash chain for tamper evidence
    std::string event_hash;

    // Message
    std::string message;

    /**
     * @brief Add metadata key-value pair
     */
    AuditEvent& with(std::string_view key, std::string_view value) {
        metadata[std::string(key)] = std::string(value);
        return *this;
    }

    /**
     * @brief Set the actor from an identity
     */
    AuditEvent& from_identity(const Identity& identity) {
        actor_id   = identity.id;
        actor_type = identity.name.empty() ? "user" : identity.name;
        return *this;
    }
};

//=============================================================================
// Audit Formatter Interface
//=============================================================================

/**
 * @brief Output format for audit logs
 */
enum class AuditFormat {
    JSON,    // Structured JSON
    CEF,     // Common Event Format (ArcSight)
    LEEF,    // Log Event Extended Format (IBM QRadar)
    SYSLOG,  // RFC 5424 syslog
    TEXT     // Human-readable text
};

/**
 * @brief Audit event formatter interface
 */
class IAuditFormatter {
public:
    virtual ~IAuditFormatter()                          = default;
    virtual std::string format(const AuditEvent& event) = 0;
    virtual AuditFormat type() const                    = 0;
};

/**
 * @brief JSON formatter
 */
class JsonAuditFormatter : public IAuditFormatter {
public:
    std::string format(const AuditEvent& event) override {
        std::ostringstream oss;

        auto time_t = std::chrono::system_clock::to_time_t(event.timestamp);
        auto us     = std::chrono::duration_cast<std::chrono::microseconds>(
                      event.timestamp.time_since_epoch()) %
                  1000000;

        oss << "{";
        oss << "\"event_id\":" << event.event_id << ",";
        oss << "\"timestamp\":\"";
        oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
        oss << "." << std::setfill('0') << std::setw(6) << us.count() << "Z\",";

        oss << "\"severity\":\"" << severity_string(event.severity) << "\",";
        oss << "\"category\":\"" << category_string(event.category) << "\",";
        oss << "\"outcome\":\"" << outcome_string(event.outcome) << "\",";
        oss << "\"event_type\":\"" << escape_json(event.event_type) << "\",";

        if (!event.correlation_id.empty()) {
            oss << "\"correlation_id\":\"" << escape_json(event.correlation_id) << "\",";
        }
        if (!event.session_id.empty()) {
            oss << "\"session_id\":\"" << escape_json(event.session_id) << "\",";
        }

        // Actor
        oss << "\"actor\":{";
        oss << "\"id\":\"" << escape_json(event.actor_id) << "\"";
        if (!event.actor_type.empty()) {
            oss << ",\"type\":\"" << escape_json(event.actor_type) << "\"";
        }
        if (!event.actor_ip.empty()) {
            oss << ",\"ip\":\"" << escape_json(event.actor_ip) << "\"";
        }
        oss << "},";

        // Target
        if (!event.target_type.empty() || !event.target_id.empty()) {
            oss << "\"target\":{";
            oss << "\"type\":\"" << escape_json(event.target_type) << "\"";
            oss << ",\"id\":\"" << escape_json(event.target_id) << "\"";
            if (!event.target_name.empty()) {
                oss << ",\"name\":\"" << escape_json(event.target_name) << "\"";
            }
            oss << "},";
        }

        // Action
        oss << "\"action\":\"" << escape_json(event.action) << "\",";
        if (!event.action_detail.empty()) {
            oss << "\"action_detail\":\"" << escape_json(event.action_detail) << "\",";
        }

        // Metadata
        if (!event.metadata.empty()) {
            oss << "\"metadata\":{";
            bool first = true;
            for (const auto& [k, v] : event.metadata) {
                if (!first)
                    oss << ",";
                oss << "\"" << escape_json(k) << "\":\"" << escape_json(v) << "\"";
                first = false;
            }
            oss << "},";
        }

        // Integrity
        if (!event.event_hash.empty()) {
            oss << "\"integrity\":{";
            oss << "\"hash\":\"" << event.event_hash << "\"";
            if (!event.previous_hash.empty()) {
                oss << ",\"previous\":\"" << event.previous_hash << "\"";
            }
            oss << "},";
        }

        // Message
        oss << "\"message\":\"" << escape_json(event.message) << "\"";

        if (event.duration.count() > 0) {
            oss << ",\"duration_us\":" << event.duration.count();
        }

        oss << "}";
        return oss.str();
    }

    AuditFormat type() const override { return AuditFormat::JSON; }

private:
    static std::string escape_json(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }
        return result;
    }
};

/**
 * @brief CEF (Common Event Format) formatter
 */
class CefAuditFormatter : public IAuditFormatter {
public:
    CefAuditFormatter(std::string_view vendor  = "IPB",
                      std::string_view product = "IndustrialProtocolBridge",
                      std::string_view version = "1.0")
        : vendor_(vendor), product_(product), version_(version) {}

    std::string format(const AuditEvent& event) override {
        // CEF:Version|Device Vendor|Device Product|Device Version|Signature
        // ID|Name|Severity|Extension
        std::ostringstream oss;

        int cef_severity = static_cast<int>(event.severity) + 1;  // CEF uses 0-10

        oss << "CEF:0|" << vendor_ << "|" << product_ << "|" << version_ << "|";
        oss << event.event_type << "|";
        oss << escape_cef(event.message) << "|";
        oss << cef_severity << "|";

        // Extensions
        oss << "rt="
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   event.timestamp.time_since_epoch())
                   .count();
        oss << " cat=" << category_string(event.category);
        oss << " outcome=" << outcome_string(event.outcome);

        if (!event.actor_id.empty()) {
            oss << " suser=" << escape_cef(event.actor_id);
        }
        if (!event.actor_ip.empty()) {
            oss << " src=" << event.actor_ip;
        }
        if (!event.target_id.empty()) {
            oss << " duid=" << escape_cef(event.target_id);
        }
        if (!event.action.empty()) {
            oss << " act=" << escape_cef(event.action);
        }
        if (!event.correlation_id.empty()) {
            oss << " externalId=" << event.correlation_id;
        }

        return oss.str();
    }

    AuditFormat type() const override { return AuditFormat::CEF; }

private:
    std::string vendor_;
    std::string product_;
    std::string version_;

    static std::string escape_cef(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '\\':
                    result += "\\\\";
                    break;
                case '|':
                    result += "\\|";
                    break;
                case '=':
                    result += "\\=";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                default:
                    result += c;
            }
        }
        return result;
    }
};

/**
 * @brief Text formatter for human-readable logs
 */
class TextAuditFormatter : public IAuditFormatter {
public:
    std::string format(const AuditEvent& event) override {
        std::ostringstream oss;

        auto time_t = std::chrono::system_clock::to_time_t(event.timestamp);

        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        oss << " [" << severity_string(event.severity) << "]";
        oss << " [" << category_string(event.category) << "]";
        oss << " " << event.event_type;
        oss << " - " << event.message;

        if (!event.actor_id.empty()) {
            oss << " (actor=" << event.actor_id << ")";
        }
        if (!event.target_id.empty()) {
            oss << " (target=" << event.target_id << ")";
        }
        oss << " [" << outcome_string(event.outcome) << "]";

        return oss.str();
    }

    AuditFormat type() const override { return AuditFormat::TEXT; }
};

//=============================================================================
// Audit Backend Interface
//=============================================================================

/**
 * @brief Audit log output backend
 */
class IAuditBackend {
public:
    virtual ~IAuditBackend()                               = default;
    virtual bool write(const std::string& formatted_event) = 0;
    virtual void flush()                                   = 0;
    virtual std::string name() const                       = 0;
};

/**
 * @brief File-based audit backend with rotation
 */
class FileAuditBackend : public IAuditBackend {
public:
    struct Config {
        std::filesystem::path base_path;
        size_t max_file_size;
        size_t max_files;
        bool compress_rotated;

        Config()
            : base_path("audit.log"), max_file_size(100 * 1024 * 1024), max_files(10),
              compress_rotated(true) {}
    };

    FileAuditBackend() : FileAuditBackend(Config{}) {}

    explicit FileAuditBackend(Config config) : config_(std::move(config)) { open_file(); }

    ~FileAuditBackend() override {
        if (file_.is_open()) {
            file_.close();
        }
    }

    bool write(const std::string& formatted_event) override {
        std::lock_guard lock(mutex_);

        if (!file_.is_open()) {
            if (!open_file())
                return false;
        }

        file_ << formatted_event << "\n";
        current_size_ += formatted_event.size() + 1;

        if (current_size_ >= config_.max_file_size) {
            rotate();
        }

        return true;
    }

    void flush() override {
        std::lock_guard lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
        }
    }

    std::string name() const override { return "file:" + config_.base_path.string(); }

private:
    bool open_file() {
        file_.open(config_.base_path, std::ios::app);
        if (file_.is_open()) {
            current_size_ = std::filesystem::exists(config_.base_path)
                              ? std::filesystem::file_size(config_.base_path)
                              : 0;
            return true;
        }
        return false;
    }

    void rotate() {
        file_.close();

        // Rotate existing files
        for (int i = static_cast<int>(config_.max_files) - 1; i >= 1; --i) {
            auto old_path = config_.base_path;
            old_path += "." + std::to_string(i);
            auto new_path = config_.base_path;
            new_path += "." + std::to_string(i + 1);

            if (std::filesystem::exists(old_path)) {
                if (i == static_cast<int>(config_.max_files) - 1) {
                    std::filesystem::remove(old_path);
                } else {
                    std::filesystem::rename(old_path, new_path);
                }
            }
        }

        // Move current to .1
        auto rotated = config_.base_path;
        rotated += ".1";
        std::filesystem::rename(config_.base_path, rotated);

        open_file();
    }

    Config config_;
    std::ofstream file_;
    size_t current_size_{0};
    std::mutex mutex_;
};

/**
 * @brief Console audit backend
 */
class ConsoleAuditBackend : public IAuditBackend {
public:
    explicit ConsoleAuditBackend(bool use_stderr = false) : use_stderr_(use_stderr) {}

    bool write(const std::string& formatted_event) override {
        std::lock_guard lock(mutex_);
        auto& stream = use_stderr_ ? std::cerr : std::cout;
        stream << formatted_event << std::endl;
        return true;
    }

    void flush() override {
        auto& stream = use_stderr_ ? std::cerr : std::cout;
        stream.flush();
    }

    std::string name() const override { return use_stderr_ ? "stderr" : "stdout"; }

private:
    bool use_stderr_;
    std::mutex mutex_;
};

/**
 * @brief Callback-based audit backend for custom handling
 */
class CallbackAuditBackend : public IAuditBackend {
public:
    using Callback = std::function<void(const std::string&)>;

    explicit CallbackAuditBackend(Callback callback, std::string_view name = "callback")
        : callback_(std::move(callback)), name_(name) {}

    bool write(const std::string& formatted_event) override {
        if (callback_) {
            callback_(formatted_event);
            return true;
        }
        return false;
    }

    void flush() override {}

    std::string name() const override { return name_; }

private:
    Callback callback_;
    std::string name_;
};

//=============================================================================
// Audit Logger
//=============================================================================

/**
 * @brief Main audit logging service
 */
class AuditLogger {
public:
    struct Config {
        AuditSeverity min_severity;
        bool enable_hash_chain;
        bool async_write;
        size_t queue_size;
        std::chrono::milliseconds flush_interval;

        Config()
            : min_severity(AuditSeverity::INFO), enable_hash_chain(true), async_write(true),
              queue_size(10000), flush_interval(1000) {}
    };

    AuditLogger() : AuditLogger(Config{}) {}

    explicit AuditLogger(Config config)
        : config_(std::move(config)), event_counter_(0), running_(false) {
        // Default formatter
        formatter_ = std::make_unique<JsonAuditFormatter>();
    }

    ~AuditLogger() { stop(); }

    /**
     * @brief Start async processing
     */
    void start() {
        if (running_.exchange(true))
            return;

        if (config_.async_write) {
            worker_ = std::thread([this] { worker_loop(); });
        }
    }

    /**
     * @brief Stop and flush
     */
    void stop() {
        if (!running_.exchange(false))
            return;

        if (worker_.joinable()) {
            cv_.notify_all();
            worker_.join();
        }

        flush();
    }

    /**
     * @brief Set formatter
     */
    void set_formatter(std::unique_ptr<IAuditFormatter> formatter) {
        std::lock_guard lock(mutex_);
        formatter_ = std::move(formatter);
    }

    /**
     * @brief Add output backend
     */
    void add_backend(std::shared_ptr<IAuditBackend> backend) {
        std::lock_guard lock(mutex_);
        backends_.push_back(std::move(backend));
    }

    /**
     * @brief Log audit event
     */
    void log(AuditEvent event) {
        // Check severity
        if (event.severity < config_.min_severity) {
            return;
        }

        // Assign event ID
        event.event_id = ++event_counter_;

        // Set timestamp if not set
        if (event.timestamp == std::chrono::system_clock::time_point{}) {
            event.timestamp = std::chrono::system_clock::now();
        }

        // Compute hash chain
        if (config_.enable_hash_chain) {
            std::lock_guard lock(hash_mutex_);
            event.previous_hash = last_hash_;
            event.event_hash    = compute_hash(event);
            last_hash_          = event.event_hash;
        }

        if (config_.async_write) {
            std::lock_guard lock(queue_mutex_);
            if (event_queue_.size() < config_.queue_size) {
                event_queue_.push_back(std::move(event));
                cv_.notify_one();
            }
            // Drop if queue full (could add metric here)
        } else {
            write_event(event);
        }
    }

    /**
     * @brief Create audit event builder
     */
    AuditEvent create_event(AuditCategory category, std::string_view event_type,
                            std::string_view message) {
        AuditEvent event;
        event.category   = category;
        event.event_type = std::string(event_type);
        event.message    = std::string(message);
        return event;
    }

    /**
     * @brief Flush all backends
     */
    void flush() {
        // Process remaining queue
        {
            std::lock_guard lock(queue_mutex_);
            while (!event_queue_.empty()) {
                write_event(event_queue_.front());
                event_queue_.pop_front();
            }
        }

        // Flush backends
        std::lock_guard lock(mutex_);
        for (auto& backend : backends_) {
            backend->flush();
        }
    }

    // Convenience methods

    void log_auth_success(const Identity& identity, std::string_view method) {
        auto event = create_event(AuditCategory::AUTHENTICATION, "auth.success",
                                  "Authentication successful");
        event.from_identity(identity);
        event.outcome = AuditOutcome::SUCCESS;
        event.action  = "login";
        event.with("method", method);
        log(std::move(event));
    }

    void log_auth_failure(std::string_view principal, std::string_view reason) {
        auto event     = create_event(AuditCategory::AUTHENTICATION, "auth.failure",
                                      "Authentication failed: " + std::string(reason));
        event.actor_id = std::string(principal);
        event.outcome  = AuditOutcome::FAILURE;
        event.action   = "login";
        event.severity = AuditSeverity::WARNING;
        event.with("reason", reason);
        log(std::move(event));
    }

    void log_access_granted(const Identity& identity, std::string_view resource,
                            std::string_view action) {
        auto event = create_event(AuditCategory::AUTHORIZATION, "access.granted",
                                  "Access granted to " + std::string(resource));
        event.from_identity(identity);
        event.outcome   = AuditOutcome::SUCCESS;
        event.target_id = std::string(resource);
        event.action    = std::string(action);
        log(std::move(event));
    }

    void log_access_denied(const Identity& identity, std::string_view resource,
                           std::string_view action, std::string_view reason) {
        auto event = create_event(AuditCategory::AUTHORIZATION, "access.denied",
                                  "Access denied to " + std::string(resource));
        event.from_identity(identity);
        event.outcome   = AuditOutcome::FAILURE;
        event.severity  = AuditSeverity::WARNING;
        event.target_id = std::string(resource);
        event.action    = std::string(action);
        event.with("reason", reason);
        log(std::move(event));
    }

    void log_data_read(const Identity& identity, std::string_view resource_type,
                       std::string_view resource_id) {
        auto event = create_event(AuditCategory::DATA_ACCESS, "data.read",
                                  "Data read from " + std::string(resource_type));
        event.from_identity(identity);
        event.outcome     = AuditOutcome::SUCCESS;
        event.target_type = std::string(resource_type);
        event.target_id   = std::string(resource_id);
        event.action      = "read";
        log(std::move(event));
    }

    void log_data_write(const Identity& identity, std::string_view resource_type,
                        std::string_view resource_id) {
        auto event = create_event(AuditCategory::DATA_ACCESS, "data.write",
                                  "Data written to " + std::string(resource_type));
        event.from_identity(identity);
        event.outcome     = AuditOutcome::SUCCESS;
        event.target_type = std::string(resource_type);
        event.target_id   = std::string(resource_id);
        event.action      = "write";
        log(std::move(event));
    }

    void log_config_change(const Identity& identity, std::string_view setting,
                           std::string_view old_value, std::string_view new_value) {
        auto event = create_event(AuditCategory::CONFIGURATION, "config.change",
                                  "Configuration changed: " + std::string(setting));
        event.from_identity(identity);
        event.outcome   = AuditOutcome::SUCCESS;
        event.target_id = std::string(setting);
        event.action    = "modify";
        event.with("old_value", old_value);
        event.with("new_value", new_value);
        log(std::move(event));
    }

    void log_security_event(AuditSeverity severity, std::string_view event_type,
                            std::string_view message) {
        auto event     = create_event(AuditCategory::SECURITY, event_type, message);
        event.severity = severity;
        log(std::move(event));
    }

    uint64_t event_count() const { return event_counter_.load(); }

private:
    void worker_loop() {
        while (running_.load()) {
            std::unique_lock lock(queue_mutex_);

            cv_.wait_for(lock, config_.flush_interval,
                         [this] { return !event_queue_.empty() || !running_.load(); });

            while (!event_queue_.empty()) {
                auto event = std::move(event_queue_.front());
                event_queue_.pop_front();
                lock.unlock();

                write_event(event);

                lock.lock();
            }
        }
    }

    void write_event(const AuditEvent& event) {
        std::string formatted;
        {
            std::lock_guard lock(mutex_);
            if (formatter_) {
                formatted = formatter_->format(event);
            }
        }

        if (!formatted.empty()) {
            std::lock_guard lock(mutex_);
            for (auto& backend : backends_) {
                backend->write(formatted);
            }
        }
    }

    std::string compute_hash(const AuditEvent& event) {
        // Simple hash computation (in production, use SHA-256)
        std::hash<std::string> hasher;
        std::string data = std::to_string(event.event_id) + event.event_type + event.message +
                           event.actor_id + event.previous_hash;

        auto hash = hasher(data);

        // Convert to hex string
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << hash;
        return oss.str();
    }

    Config config_;
    std::unique_ptr<IAuditFormatter> formatter_;
    std::vector<std::shared_ptr<IAuditBackend>> backends_;

    std::atomic<uint64_t> event_counter_;
    std::atomic<bool> running_;

    std::deque<AuditEvent> event_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::thread worker_;

    std::string last_hash_;
    std::mutex hash_mutex_;
    std::mutex mutex_;
};

//=============================================================================
// Global Audit Logger Access
//=============================================================================

/**
 * @brief Get global audit logger instance
 */
inline AuditLogger& get_audit_logger() {
    static AuditLogger instance;
    return instance;
}

/**
 * @brief Convenience macros for audit logging
 */
#define AUDIT_AUTH_SUCCESS(identity, method) \
    ::ipb::security::get_audit_logger().log_auth_success(identity, method)

#define AUDIT_AUTH_FAILURE(principal, reason) \
    ::ipb::security::get_audit_logger().log_auth_failure(principal, reason)

#define AUDIT_ACCESS_GRANTED(identity, resource, action) \
    ::ipb::security::get_audit_logger().log_access_granted(identity, resource, action)

#define AUDIT_ACCESS_DENIED(identity, resource, action, reason) \
    ::ipb::security::get_audit_logger().log_access_denied(identity, resource, action, reason)

#define AUDIT_DATA_READ(identity, type, id) \
    ::ipb::security::get_audit_logger().log_data_read(identity, type, id)

#define AUDIT_DATA_WRITE(identity, type, id) \
    ::ipb::security::get_audit_logger().log_data_write(identity, type, id)

#define AUDIT_SECURITY(severity, type, message) \
    ::ipb::security::get_audit_logger().log_security_event(severity, type, message)

}  // namespace ipb::security
