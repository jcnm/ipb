#pragma once

/**
 * @file http_backend.hpp
 * @brief Abstract HTTP backend interface
 *
 * Defines the interface that all HTTP backends must implement.
 * Supports multiple implementations:
 * - libcurl (default, most portable)
 * - Boost.Beast (high-performance, header-only)
 * - Native (future, minimal dependencies)
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ipb::transport::http {

//=============================================================================
// Forward Declarations
//=============================================================================

struct HTTPConfig;

//=============================================================================
// Backend Types
//=============================================================================

/**
 * @brief Available HTTP backend implementations
 */
enum class BackendType {
    CURL,   ///< libcurl (default, portable)
    BEAST,  ///< Boost.Beast (high-performance)
    NATIVE  ///< Native implementation (future)
};

/**
 * @brief Get backend type name
 */
constexpr std::string_view backend_type_name(BackendType type) noexcept {
    switch (type) {
        case BackendType::CURL:
            return "curl";
        case BackendType::BEAST:
            return "beast";
        case BackendType::NATIVE:
            return "native";
        default:
            return "unknown";
    }
}

//=============================================================================
// HTTP Methods and Status
//=============================================================================

/**
 * @brief HTTP methods
 */
enum class Method : uint8_t { GET, POST, PUT, PATCH, DELETE_, HEAD, OPTIONS };

/**
 * @brief Get HTTP method string
 */
constexpr std::string_view method_to_string(Method method) noexcept {
    switch (method) {
        case Method::GET:
            return "GET";
        case Method::POST:
            return "POST";
        case Method::PUT:
            return "PUT";
        case Method::PATCH:
            return "PATCH";
        case Method::DELETE_:
            return "DELETE";
        case Method::HEAD:
            return "HEAD";
        case Method::OPTIONS:
            return "OPTIONS";
        default:
            return "GET";
    }
}

/**
 * @brief HTTP status code categories
 */
enum class StatusCategory : uint8_t {
    INFORMATIONAL,  // 1xx
    SUCCESS,        // 2xx
    REDIRECTION,    // 3xx
    CLIENT_ERROR,   // 4xx
    SERVER_ERROR    // 5xx
};

/**
 * @brief Get status category from code
 */
constexpr StatusCategory status_category(int code) noexcept {
    if (code >= 100 && code < 200)
        return StatusCategory::INFORMATIONAL;
    if (code >= 200 && code < 300)
        return StatusCategory::SUCCESS;
    if (code >= 300 && code < 400)
        return StatusCategory::REDIRECTION;
    if (code >= 400 && code < 500)
        return StatusCategory::CLIENT_ERROR;
    return StatusCategory::SERVER_ERROR;
}

//=============================================================================
// Request and Response
//=============================================================================

using Headers = std::map<std::string, std::string>;

/**
 * @brief HTTP Request
 */
struct Request {
    Method method = Method::GET;
    std::string url;
    Headers headers;
    std::vector<uint8_t> body;

    // Timeouts
    std::chrono::milliseconds connect_timeout{30000};
    std::chrono::milliseconds timeout{60000};

    // TLS options
    bool verify_ssl = true;
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;

    // Follow redirects
    bool follow_redirects = true;
    int max_redirects     = 10;

    // HTTP version
    bool use_http2 = true;

    // Helper methods
    void set_json_content() { headers["Content-Type"] = "application/json"; }

    void set_form_content() { headers["Content-Type"] = "application/x-www-form-urlencoded"; }

    void set_body(std::string_view data) { body.assign(data.begin(), data.end()); }
};

/**
 * @brief HTTP Response
 */
struct Response {
    int status_code = 0;
    std::string status_message;
    Headers headers;
    std::vector<uint8_t> body;

    // Timing info
    std::chrono::microseconds total_time{0};
    std::chrono::microseconds connect_time{0};

    // Error info
    std::string error_message;

    // Helper methods
    bool is_success() const noexcept { return status_code >= 200 && status_code < 300; }

    bool is_redirect() const noexcept { return status_code >= 300 && status_code < 400; }

    bool is_client_error() const noexcept { return status_code >= 400 && status_code < 500; }

    bool is_server_error() const noexcept { return status_code >= 500; }

    std::string body_string() const { return std::string(body.begin(), body.end()); }

    std::string_view get_header(const std::string& name) const {
        auto it = headers.find(name);
        if (it != headers.end()) {
            return it->second;
        }
        return {};
    }
};

//=============================================================================
// Callbacks
//=============================================================================

/**
 * @brief Response callback (for async operations)
 */
using ResponseCallback = std::function<void(Response response)>;

/**
 * @brief Progress callback
 * @param download_total Total bytes to download (0 if unknown)
 * @param download_now Bytes downloaded so far
 * @param upload_total Total bytes to upload
 * @param upload_now Bytes uploaded so far
 * @return true to continue, false to abort
 */
using ProgressCallback = std::function<bool(size_t download_total, size_t download_now,
                                            size_t upload_total, size_t upload_now)>;

//=============================================================================
// Backend Statistics
//=============================================================================

/**
 * @brief Backend statistics
 */
struct BackendStats {
    uint64_t requests_sent      = 0;
    uint64_t responses_received = 0;
    uint64_t requests_failed    = 0;
    uint64_t bytes_sent         = 0;
    uint64_t bytes_received     = 0;

    // Timing statistics
    uint64_t total_request_time_us = 0;

    constexpr void reset() noexcept {
        requests_sent         = 0;
        responses_received    = 0;
        requests_failed       = 0;
        bytes_sent            = 0;
        bytes_received        = 0;
        total_request_time_us = 0;
    }

    constexpr uint64_t avg_request_time_us() const noexcept {
        return responses_received > 0 ? total_request_time_us / responses_received : 0;
    }
};

//=============================================================================
// IHTTPBackend Interface
//=============================================================================

/**
 * @brief Abstract HTTP backend interface
 */
class IHTTPBackend {
public:
    virtual ~IHTTPBackend() = default;

    //=========================================================================
    // Backend Info
    //=========================================================================

    /**
     * @brief Get backend type
     */
    virtual BackendType type() const noexcept = 0;

    /**
     * @brief Get backend name
     */
    virtual std::string_view name() const noexcept = 0;

    /**
     * @brief Get backend version string
     */
    virtual std::string_view version() const noexcept = 0;

    /**
     * @brief Check if backend supports HTTP/2
     */
    virtual bool supports_http2() const noexcept = 0;

    //=========================================================================
    // Request Execution
    //=========================================================================

    /**
     * @brief Execute HTTP request (synchronous)
     * @param request The request to execute
     * @return Response
     */
    virtual Response execute(const Request& request) = 0;

    /**
     * @brief Execute HTTP request (asynchronous)
     * @param request The request to execute
     * @param callback Callback for response
     */
    virtual void execute_async(const Request& request, ResponseCallback callback) = 0;

    //=========================================================================
    // Connection Management
    //=========================================================================

    /**
     * @brief Close all connections
     */
    virtual void close_all() = 0;

    /**
     * @brief Set progress callback
     */
    virtual void set_progress_callback(ProgressCallback callback) = 0;

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get backend statistics
     */
    virtual const BackendStats& stats() const noexcept = 0;

    /**
     * @brief Reset statistics
     */
    virtual void reset_stats() noexcept = 0;
};

//=============================================================================
// Backend Factory
//=============================================================================

/**
 * @brief Create HTTP backend instance
 * @param type Backend type to create
 * @return Unique pointer to backend, or nullptr if type not available
 */
std::unique_ptr<IHTTPBackend> create_backend(BackendType type);

/**
 * @brief Get default backend type (based on compile-time configuration)
 */
BackendType default_backend_type() noexcept;

/**
 * @brief Check if backend type is available
 */
bool is_backend_available(BackendType type) noexcept;

}  // namespace ipb::transport::http
